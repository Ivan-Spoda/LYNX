# Gera o ícone do LYNX (disco escuro + pimenta) a partir do PNG transparente da pimenta.
# Uso: python3 tools/make_logo.py resources/lynx_logo.png pasta_de_saida
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops
S = 1024
out = sys.argv[2]
pepper = Image.open(sys.argv[1]).convert("RGBA")
pepper = pepper.crop(pepper.getbbox())

def radial(size, inner, outer, center=(0.5, 0.42)):
    # gradiente radial (centro levemente acima: luz de cima)
    import math
    img = Image.new("RGBA", (size, size))
    px = img.load()
    cx, cy = center[0] * size, center[1] * size
    maxd = size * 0.62
    for y in range(size):
        for x in range(size):
            t = min(1.0, math.hypot(x - cx, y - cy) / maxd)
            t = t * t
            px[x, y] = tuple(int(inner[i] + (outer[i] - inner[i]) * t) for i in range(4))
    return img

big = S * 2  # supersample para bordas suaves
mask = Image.new("L", (big, big), 0)
ImageDraw.Draw(mask).ellipse((40, 40, big - 40, big - 40), fill=255)
disc = radial(big, (52, 52, 56, 255), (8, 8, 10, 255))
badge = Image.new("RGBA", (big, big), (0, 0, 0, 0))
badge.paste(disc, (0, 0), mask)

# aro: anel fino com degradê laranja->vermelho (as cores da pimenta), mais forte em cima
rim = Image.new("RGBA", (big, big), (0, 0, 0, 0))
d = ImageDraw.Draw(rim)
for i, w in enumerate(range(0, 22)):
    a = int(230 * (1 - i / 22))
    d.ellipse((40 + i, 40 + i, big - 40 - i, big - 40 - i), outline=(232, 74, 28, a), width=1)
rimGrad = Image.linear_gradient("L").resize((big, big)).point(lambda v: 255 - int(v * 0.65))
rim.putalpha(ImageChops.multiply(rim.getchannel("A"), rimGrad))
badge = Image.alpha_composite(badge, rim)

# brilho de vidro na metade de cima
gloss = Image.new("L", (big, big), 0)
ImageDraw.Draw(gloss).ellipse((170, 70, big - 170, big * 0.52), fill=34)
gloss = gloss.filter(ImageFilter.GaussianBlur(60))
gl = Image.new("RGBA", (big, big), (255, 255, 255, 0)); gl.putalpha(ImageChops.multiply(gloss, mask))
badge = Image.alpha_composite(badge, gl)

# pimenta: ~74% da altura, levemente inclinada, com sombra
h = int(big * 0.74)
w = int(pepper.width * h / pepper.height)
p = pepper.resize((w, h), Image.LANCZOS).rotate(-14, resample=Image.BICUBIC, expand=True)
px, py = (big - p.width) // 2 + 10, (big - p.height) // 2 + 12
shadow = Image.new("RGBA", (big, big), (0, 0, 0, 0))
sh = Image.new("RGBA", p.size, (0, 0, 0, 255)); sh.putalpha(p.getchannel("A").point(lambda a: int(a * 0.7)))
shadow.paste(sh, (px + 26, py + 34), sh)
shadow = shadow.filter(ImageFilter.GaussianBlur(28))
shadow.putalpha(ImageChops.multiply(shadow.getchannel("A"), mask))
badge = Image.alpha_composite(badge, shadow)
layer = Image.new("RGBA", (big, big), (0, 0, 0, 0)); layer.paste(p, (px, py), p)
badge = Image.alpha_composite(badge, layer)

final = badge.resize((S, S), Image.LANCZOS)
final.save(out + "/lynx_badge_1024.png")
for s in [16, 20, 24, 32, 40, 48, 64, 96, 128, 256, 512]:
    final.resize((s, s), Image.LANCZOS).save(f"{out}/badge_{s}.png")
print("ok")
