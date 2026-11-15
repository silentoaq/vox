import sys

import demucs.demucs
import numpy as np
import onnxruntime as ort
import torch
from demucs.pretrained import get_model

out_path = sys.argv[1]
SEGMENT = 1024 * int(sys.argv[2])

model = get_model("hdemucs_mmi").models[0].eval()
assert all(not m.nfreqs for m in model.modules() if isinstance(m, demucs.demucs.LocalState))

torch.manual_seed(0)
mix = torch.randn(1, 2, SEGMENT) * 0.1
with torch.no_grad():
    reference = model(mix)


def eye_free_forward(self, x):
    B, C, T = x.shape
    heads = self.heads
    indexes = torch.arange(T, device=x.device, dtype=x.dtype)
    delta = indexes[:, None] - indexes[None, :]
    queries = self.query(x).view(B, heads, -1, T)
    keys = self.key(x).view(B, heads, -1, T)
    dots = torch.einsum("bhct,bhcs->bhts", keys, queries)
    dots = dots / keys.shape[2] ** 0.5
    if self.ndecay:
        decays = torch.arange(1, self.ndecay + 1, device=x.device, dtype=x.dtype)
        decay_q = self.query_decay(x).view(B, heads, -1, T)
        decay_q = torch.sigmoid(decay_q) / 2
        decay_kernel = -decays.view(-1, 1, 1) * delta.abs() / self.ndecay ** 0.5
        dots = dots + torch.einsum("fts,bhfs->bhts", decay_kernel, decay_q)
    dots = dots.masked_fill(delta == 0, -100)
    weights = torch.softmax(dots, dim=2)
    content = self.content(x).view(B, heads, -1, T)
    result = torch.einsum("bhts,bhct->bhcs", weights, content)
    result = result.reshape(B, -1, T)
    return x + self.proj(result)


demucs.demucs.LocalState.forward = eye_free_forward


class Core(torch.nn.Module):
    def __init__(self, m):
        super().__init__()
        self.m = m

    def forward(self, mag, mix):
        m = self.m
        x = mag
        B, C, Fq, T = x.shape
        mean = x.mean(dim=(1, 2, 3), keepdim=True)
        std = x.std(dim=(1, 2, 3), keepdim=True)
        x = (x - mean) / (1e-5 + std)
        xt = mix
        meant = xt.mean(dim=(1, 2), keepdim=True)
        stdt = xt.std(dim=(1, 2), keepdim=True)
        xt = (xt - meant) / (1e-5 + stdt)
        saved, saved_t, lengths, lengths_t = [], [], [], []
        for idx, encode in enumerate(m.encoder):
            lengths.append(x.shape[-1])
            inject = None
            if idx < len(m.tencoder):
                lengths_t.append(xt.shape[-1])
                tenc = m.tencoder[idx]
                xt = tenc(xt)
                if not tenc.empty:
                    saved_t.append(xt)
                else:
                    inject = xt
            x = encode(x, inject)
            if idx == 0 and m.freq_emb is not None:
                frs = torch.arange(x.shape[-2], device=x.device)
                emb = m.freq_emb(frs).t()[None, :, :, None].expand_as(x)
                x = x + m.freq_emb_scale * emb
            saved.append(x)
        x = torch.zeros_like(x)
        xt = torch.zeros_like(x)
        for idx, decode in enumerate(m.decoder):
            skip = saved.pop(-1)
            x, pre = decode(x, skip, lengths.pop(-1))
            offset = m.depth - len(m.tdecoder)
            if idx >= offset:
                tdec = m.tdecoder[idx - offset]
                length_t = lengths_t.pop(-1)
                if tdec.empty:
                    pre = pre[:, :, 0]
                    xt, _ = tdec(pre, None, length_t)
                else:
                    skip = saved_t.pop(-1)
                    xt, _ = tdec(xt, skip, length_t)
        S = len(m.sources)
        x = x.view(B, S, -1, Fq, T)
        x = x * std[:, None] + mean[:, None]
        xt = xt.view(B, S, -1, mix.shape[-1])
        xt = xt * stdt[:, None] + meant[:, None]
        return x, xt


core = Core(model).eval()
with torch.no_grad():
    z = model._spec(mix)
    mag = model._magnitude(z)
    torch.onnx.export(core, (mag, mix), out_path, input_names=["mag", "mix"], output_names=["spec", "wave"],
                      opset_version=17, dynamo=False)
    spec, wave = core(mag, mix)
    rebuilt = model._ispec(model._mask(z, spec), SEGMENT) + wave
    print("mag", tuple(mag.shape), "spec", tuple(spec.shape), "wave", tuple(wave.shape))
    print("patched split vs original max diff", float((rebuilt - reference).abs().max()))

session = ort.InferenceSession(out_path, providers=["CPUExecutionProvider"])
o_spec, o_wave = session.run(None, {"mag": mag.numpy(), "mix": mix.numpy()})
print("onnx spec max diff", float(np.abs(o_spec - spec.numpy()).max()), "wave max diff", float(np.abs(o_wave - wave.numpy()).max()))
