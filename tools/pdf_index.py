# re-encode the PDF's screenshots as 256-colour indexed images (they're already 256 colours: lossless)
import sys, zlib, pikepdf
from pikepdf import PdfImage, Name
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
pdf = pikepdf.open(src)
done = set()
for page in pdf.pages:
    for key, raw in page.get_images().items() if hasattr(page, 'get_images') else page.images.items():
        if raw.objgen in done: continue
        done.add(raw.objgen)
        try:
            im = PdfImage(raw).as_pil_image().convert('RGB')
        except Exception as e:
            print('skip', e); continue
        if '/SMask' in raw: continue
        q = im.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        pal = q.getpalette()[:256*3]
        pal += [0] * (768 - len(pal))
        data = zlib.compress(q.tobytes(), 9)
        raw.write(data, filter=Name.FlateDecode)
        raw.ColorSpace = pikepdf.Array([Name.Indexed, Name.DeviceRGB, 255, pikepdf.String(bytes(pal))])
        raw.BitsPerComponent = 8
        for k in ('/DecodeParms', '/Decode'):
            if k in raw: del raw[k]
pdf.save(dst, compress_streams=True, object_stream_mode=pikepdf.ObjectStreamMode.generate)
