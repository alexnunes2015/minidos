#!/usr/bin/env python3
"""
Create a demo boot logo for MiniDOS (Windows 95 style)
320x200, 256 colors
"""

from PIL import Image, ImageDraw, ImageFont
import sys

# Create image
width, height = 320, 200
img = Image.new('RGB', (width, height), color=(0, 128, 128))  # Teal background (Win95 style)

draw = ImageDraw.Draw(img)

# Draw gradient background
for y in range(height):
    intensity = int(128 + (y / height) * 127)
    color = (0, intensity // 2, intensity)
    draw.line([(0, y), (width, y)], fill=color)

# Draw border (Windows 95 style)
border_color = (192, 192, 192)  # Light gray
draw.rectangle([10, 10, width-11, height-11], outline=border_color, width=2)
draw.rectangle([12, 12, width-13, height-13], outline=(255, 255, 255), width=1)

# Draw title box
title_bg = (0, 0, 128)  # Dark blue (Windows title bar)
draw.rectangle([20, 30, width-20, 70], fill=title_bg)
draw.rectangle([20, 30, width-20, 70], outline=(255, 255, 255), width=1)

# Draw text
try:
    # Try to use a decent font
    font_large = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 24)
    font_small = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 14)
except:
    # Fallback to default font
    font_large = ImageFont.load_default()
    font_small = ImageFont.load_default()

# Title
title_text = "MiniDOS"
title_bbox = draw.textbbox((0, 0), title_text, font=font_large)
title_width = title_bbox[2] - title_bbox[0]
title_x = (width - title_width) // 2
draw.text((title_x, 40), title_text, fill=(255, 255, 255), font=font_large)

# Subtitle
subtitle = "Version 0.1 MVP"
sub_bbox = draw.textbbox((0, 0), subtitle, font=font_small)
sub_width = sub_bbox[2] - sub_bbox[0]
sub_x = (width - sub_width) // 2
draw.text((sub_x, 90), subtitle, fill=(255, 255, 255), font=font_small)

# System info
info_text = "32-bit Protected Mode OS"
info_bbox = draw.textbbox((0, 0), info_text, font=font_small)
info_width = info_bbox[2] - info_bbox[0]
info_x = (width - info_width) // 2
draw.text((info_x, 110), info_text, fill=(200, 200, 200), font=font_small)

# Progress bar (Windows 95 style)
bar_x = 60
bar_y = 150
bar_width = 200
bar_height = 20

# Progress bar background
draw.rectangle([bar_x, bar_y, bar_x + bar_width, bar_y + bar_height], 
               fill=(64, 64, 64), outline=(255, 255, 255), width=1)

# Progress bar fill
for i in range(8):
    segment_x = bar_x + 2 + (i * 24)
    draw.rectangle([segment_x, bar_y + 2, segment_x + 20, bar_y + bar_height - 2],
                   fill=(0, 0, 192))  # Blue segments

# Status text
status = "Starting..."
status_bbox = draw.textbbox((0, 0), status, font=font_small)
status_width = status_bbox[2] - status_bbox[0]
status_x = (width - status_width) // 2
draw.text((status_x, 175), status, fill=(255, 255, 255), font=font_small)

# Convert to 256 colors palette
img = img.convert('P', palette=Image.ADAPTIVE, colors=256)

# Save as BMP
output_file = 'boot_logo.bmp'
img.save(output_file, 'BMP')
print(f"✓ Created demo logo: {output_file}")
print(f"  Resolution: {width}x{height}")
print(f"  Colors: 256 (indexed)")
print("")
print("Now run: ./convert_logo.sh boot_logo.bmp")
