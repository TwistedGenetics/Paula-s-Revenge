# A reference for the app's STEMS: a fixed 12-second mix separated by PyTorch Demucs (htdemucs,
# no random shift, split into 7.8 s pieces with 25% overlap - what the app does), and the same
# first piece run through the ONNX model, to check the export.
import sys, struct
import numpy as np
import torch
from demucs.pretrained import get_model
from demucs.apply import apply_model
import onnxruntime as ort
from demucs.htdemucs import standalone_spec, standalone_magnitude

out = sys.argv[1]
sr = 44100
n = sr * 12
t = np.arange(n) / sr
rng = np.random.default_rng(1234)
# drums: noise bursts and a kick every half second; bass: a low sine; "other": a chord; "vocals": a vibrato tone
kick = np.zeros(n); burst = np.zeros(n)
for k in range(0, n, sr // 2):
    m = min(n - k, 6000)
    kick[k:k + m] += np.sin(2 * np.pi * 55 * np.arange(m) / sr) * np.exp(-np.arange(m) / 1500)
    j = k + sr // 4
    if j < n:
        m2 = min(n - j, 3000)
        burst[j:j + m2] += rng.standard_normal(m2) * np.exp(-np.arange(m2) / 600) * 0.5
bass = 0.3 * np.sin(2 * np.pi * 41.2 * t) * (0.6 + 0.4 * np.sign(np.sin(2 * np.pi * 0.5 * t)))
chord = 0.08 * sum(np.sin(2 * np.pi * f * t) for f in (261.6, 329.6, 392.0))
voice = 0.15 * np.sin(2 * np.pi * 440 * t + 3 * np.sin(2 * np.pi * 5 * t)) * (np.sin(2 * np.pi * 0.25 * t) > 0)
left = kick + burst + bass + chord + voice
right = kick + 0.8 * burst + bass + 0.7 * chord + voice
mix = np.stack([left, right]).astype(np.float32)

model = get_model('htdemucs')
wav = torch.tensor(mix)
ref = wav.mean(0)
norm = (wav - ref.mean()) / ref.std()
with torch.no_grad():
    stems = apply_model(model, norm[None], shifts=0, split=True, overlap=0.25, progress=False)[0]
stems = stems * ref.std() + ref.mean()
print("sources", model.sources, "stems", tuple(stems.shape))

# the ONNX model on the first 7.8 s piece, against PyTorch's core model on the same piece
core = model if not hasattr(model, 'models') else model.models[0]
S = int(core.segment * core.samplerate)
piece = norm[:, :S][None]
with torch.no_grad():
    mag = standalone_magnitude(standalone_spec(piece))
    tx, txt = core(piece, mag)
sess = ort.InferenceSession(f"{out}/htdemucs.onnx")
names = [i.name for i in sess.get_inputs()]
ox, oxt = sess.run(None, { names[0]: piece.numpy(), names[1]: mag.numpy() })
print("onnx vs torch: spec max diff", float(np.abs(ox - tx.numpy()).max()), "time max diff", float(np.abs(oxt - txt.numpy()).max()))
print("onnx inputs", [(i.name, i.shape) for i in sess.get_inputs()], "outputs", [(o.name, o.shape) for o in sess.get_outputs()])

with open(f"{out}/reference.bin", "wb") as f:
    f.write(struct.pack("<4i", sr, n, 2, stems.shape[0]))
    f.write(mix.tobytes())
    f.write(stems.numpy().astype(np.float32).tobytes())
