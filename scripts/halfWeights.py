import sys

import numpy as np
import onnx
import onnxruntime as ort
from onnx import helper, numpy_helper

source, target = sys.argv[1], sys.argv[2]
model = onnx.load(source)
graph = model.graph
kept, casts = [], []
for initializer in graph.initializer:
    array = numpy_helper.to_array(initializer)
    if array.dtype != np.float32 or array.size < 1024:
        kept.append(initializer)
        continue
    half = numpy_helper.from_array(array.astype(np.float16), initializer.name + "_fp16")
    kept.append(half)
    casts.append(helper.make_node("Cast", [half.name], [initializer.name], to=onnx.TensorProto.FLOAT))
del graph.initializer[:]
graph.initializer.extend(kept)
nodes = list(graph.node)
del graph.node[:]
graph.node.extend(casts + nodes)
onnx.save(model, target)

options = ort.SessionOptions()
options.intra_op_num_threads = 2
full = ort.InferenceSession(source, options, providers=["CPUExecutionProvider"])
half = ort.InferenceSession(target, options, providers=["CPUExecutionProvider"])
rng = np.random.default_rng(0)
shapes = {i.name: i.shape for i in full.get_inputs()}
inputs = {"mag": rng.standard_normal(shapes["mag"]).astype(np.float32),
          "mix": (rng.standard_normal(shapes["mix"]) * 0.1).astype(np.float32)}
a = full.run(None, inputs)
b = half.run(None, inputs)
for name, x, y in zip(["spec", "wave"], a, b):
    print(name, "max diff", float(np.abs(x - y).max()), "relative", float(np.abs(x - y).max() / np.abs(x).max()))
