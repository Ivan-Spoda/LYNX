# Generates the LYNX project-file icon (a dark page with a folded corner, a red band and the LYNX badge).
# Usage: python3 tools/make_doc_icon.py resources/lynx_icon_256.png resources/LYNX_doc.ico [preview.png]
import sys
from PIL import Image, ImageDraw, ImageFilter

S = 1024
badge = Image.open(sys.argv[1]).convert("RGBA")

page = Image.new("RGBA", (S, S), (0, 0, 0, 0))
left, right, top, bottom = 150, 874, 40, 984
fold = 230
outline = [(left, top), (right - fold, top), (right, top + fold), (right, bottom), (left, bottom)]

# soft shadow under the page
shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
ImageDraw.Draw(shadow).polygon([(x + 14, y + 20) for x, y in outline], fill=(0, 0, 0, 120))
page = Image.alpha_composite(page, shadow.filter(ImageFilter.GaussianBlur(22)))

body = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(body)
d.polygon(outline, fill=(38, 38, 43, 255))
# vertical sheen: lighter at the top
sheen = Image.linear_gradient("L").resize((S, S)).point(lambda v: int(40 * (1 - v / 255)))
light = Image.new("RGBA", (S, S), (255, 255, 255, 0))
light.putalpha(sheen)
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).polygon(outline, fill=255)
light.putalpha(Image.composite(light.getchannel("A"), Image.new("L", (S, S), 0), mask))
body = Image.alpha_composite(body, light)
d = ImageDraw.Draw(body)
# red band at the bottom (the accent colour)
d.rectangle((left, bottom - 150, right, bottom), fill=(214, 40, 40, 255))
d.rectangle((left, bottom - 150, right, bottom - 138), fill=(240, 80, 70, 255))
# folded corner
d.polygon([(right - fold, top), (right - fold, top + fold), (right, top + fold)], fill=(78, 78, 86, 255))
d.line([(right - fold, top), (right, top + fold)], fill=(20, 20, 24, 255), width=6)
# thin border
d.line(outline + [outline[0]], fill=(90, 90, 98, 255), width=8, joint="curve")
page = Image.alpha_composite(page, body)

# the LYNX badge in the middle of the page
size = 560
b = badge.resize((size, size), Image.LANCZOS)
page.alpha_composite(b, ((left + right - size) // 2, top + fold - 40))

sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
page.save(sys.argv[2], format="ICO", sizes=[(s, s) for s in sizes])
if len(sys.argv) > 3:
    page.resize((256, 256), Image.LANCZOS).save(sys.argv[3])
print("ok")
