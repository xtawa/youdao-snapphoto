"""Render the editable, geometric SnapPhoto launcher icon with transparent corners."""
from pathlib import Path
from PIL import Image, ImageDraw


def render_icon(destination):
    scale = 4
    image = Image.new('RGBA', (128 * scale, 128 * scale))
    draw = ImageDraw.Draw(image)

    def box(bounds):
        return tuple(int(value * scale) for value in bounds)

    def rounded(bounds, radius, fill):
        draw.rounded_rectangle(box(bounds), radius=radius * scale, fill=fill)

    # A small transparent inset keeps the antialiased edge clear on the launcher.
    rounded((3, 3, 125, 125), 29, '#395a5b')
    rounded((4, 4, 124, 124), 28, '#182f33')
    rounded((42, 29, 75, 48), 6, '#c7e9df')
    rounded((23, 40, 105, 98), 13, '#c7e9df')
    draw.ellipse(box((43, 49, 85, 91)), fill='#182f33')
    draw.ellipse(box((48, 54, 80, 86)), fill='#649b92')
    draw.ellipse(box((53, 59, 75, 81)), fill='#294c50')
    draw.ellipse(box((56, 60, 63, 67)), fill='#c7e9df')
    rounded((87, 49, 97, 55), 3, '#649b92')
    image.resize((128, 128), Image.Resampling.LANCZOS).save(destination)


if __name__ == '__main__':
    render_icon(Path(__file__).resolve().parent.parent / 'app_icon.png')
