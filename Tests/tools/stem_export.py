# Demucs v4 to ONNX for the app's STEMS (the STFT outside the network: sevagh/demucs.onnx's
# demucs-for-onnx): htdemucs, and the four fine-tuned htdemucs_ft models - each one the bag's
# specialist for one source (drums, bass, other, vocals).
import sys
from pathlib import Path
import torch
from torch.nn import functional as F
from demucs.pretrained import get_model
from demucs.htdemucs import HTDemucs, standalone_spec, standalone_magnitude

out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)

def export(core, name):
    S = int(core.segment * core.samplerate)
    wave = torch.randn(1, 2, S)
    mag = standalone_magnitude(standalone_spec(wave))
    torch.onnx.export(core, (wave, mag), out / f"{name}.onnx", export_params=True, opset_version=17,
                      do_constant_folding=True, input_names=['mix', 'spec'], output_names=['spec_out', 'mix_out'])
    print("exported", name, "segment", S)

m = get_model('htdemucs')
export(m.models[0] if hasattr(m, 'models') else m, 'htdemucs')

bag = get_model('htdemucs_ft')
print("sources", bag.sources, "weights", bag.weights)
for i, sub in enumerate(bag.models):
    w = bag.weights[i]
    src = max(range(len(w)), key=lambda k: w[k])
    export(sub, f"htdemucs_ft_{bag.sources[src]}")
