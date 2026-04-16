#!/usr/bin/env python3
"""
Convert a directory of PNG images into an animated GIF.
Useful for visualizing VLSI placement optimization progress.
"""

import argparse
import os
from pathlib import Path
from PIL import Image
import re


def natural_sort_key(filename):
    """
    Sort filenames naturally (e.g., img1.png, img2.png, img10.png).
    Handles common naming patterns like frame_001.png, iteration_0042.png, etc.
    """
    # Extract numbers from filename and convert to integers for proper sorting
    return [int(text) if text.isdigit() else text.lower() 
            for text in re.split(r'(\d+)', str(filename))]


def create_gif(input_dir, output_file, duration=100, loop=0, optimize=True, resize=None, quiet=False):
    """
    Create an animated GIF from PNG files in a directory.

    Args:
        input_dir: Directory containing PNG images
        output_file: Output GIF filename
        duration: Duration of each frame in milliseconds (default: 100ms = 10fps)
        loop: Number of loops (0 = infinite loop)
        optimize: Whether to optimize the GIF (reduces file size)
        resize: Optional tuple (width, height) to resize images
        quiet: If True, suppress all print output
    """
    def log(msg):
        if not quiet:
            print(msg)

    input_path = Path(input_dir)

    if not input_path.exists():
        raise FileNotFoundError(f"Directory not found: {input_dir}")

    # Get all PNG files and sort them naturally
    png_files = sorted(input_path.glob("*.png"), key=natural_sort_key)

    if not png_files:
        raise ValueError(f"No PNG files found in {input_dir}")

    log(f"Found {len(png_files)} PNG files")

    # Load images
    images = []
    for png_file in png_files:
        img = Image.open(png_file)

        # Convert to RGB if necessary (GIF doesn't support RGBA)
        if img.mode != 'RGB':
            img = img.convert('RGB')

        # Resize if requested
        if resize:
            img = img.resize(resize, Image.Resampling.LANCZOS)

        images.append(img)
        log(f"Loaded: {png_file.name}")

    # Save as GIF
    log(f"\nCreating GIF: {output_file}")
    log(f"  Duration per frame: {duration}ms ({1000/duration:.1f} fps)")
    log(f"  Total frames: {len(images)}")
    log(f"  Loop count: {'infinite' if loop == 0 else loop}")

    images[0].save(
        output_file,
        save_all=True,
        append_images=images[1:],
        duration=duration,
        loop=loop,
        optimize=optimize
    )

    # Report file size
    size_mb = os.path.getsize(output_file) / (1024 * 1024)
    log(f"\nGIF created successfully: {output_file}")
    log(f"File size: {size_mb:.2f} MB")


def main():
    parser = argparse.ArgumentParser(
        description="Convert PNG images to animated GIF for placement visualization",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Basic usage - create GIF from PNGs in current directory
  python png_to_gif.py ./frames -o placement.gif
  
  # Faster animation (50ms per frame = 20fps)
  python png_to_gif.py ./frames -o placement.gif -d 50
  
  # Slower animation for detailed inspection (500ms = 2fps)
  python png_to_gif.py ./frames -o placement.gif -d 500
  
  # Resize to smaller dimensions to reduce file size
  python png_to_gif.py ./frames -o placement.gif --resize 800 600
  
  # Play once instead of looping
  python png_to_gif.py ./frames -o placement.gif --loop 1
        """
    )
    
    parser.add_argument(
        'input_dir',
        help='Directory containing PNG images'
    )
    
    parser.add_argument(
        '-o', '--output',
        default='output.gif',
        help='Output GIF filename (default: output.gif)'
    )
    
    parser.add_argument(
        '-d', '--duration',
        type=int,
        default=100,
        help='Duration of each frame in milliseconds (default: 100ms = 10fps)'
    )
    
    parser.add_argument(
        '-l', '--loop',
        type=int,
        default=0,
        help='Number of loops (0 = infinite, default: 0)'
    )
    
    parser.add_argument(
        '--no-optimize',
        action='store_true',
        help='Disable GIF optimization (faster but larger file)'
    )
    
    parser.add_argument(
        '--resize',
        nargs=2,
        type=int,
        metavar=('WIDTH', 'HEIGHT'),
        help='Resize images to specified dimensions'
    )

    parser.add_argument(
        '-q', '--quiet',
        action='store_true',
        help='Suppress all print output'
    )

    args = parser.parse_args()

    # Convert resize tuple if provided
    resize = tuple(args.resize) if args.resize else None

    try:
        create_gif(
            input_dir=args.input_dir,
            output_file=args.output,
            duration=args.duration,
            loop=args.loop,
            optimize=not args.no_optimize,
            resize=resize,
            quiet=args.quiet
        )
    except Exception as e:
        print(f"Error: {e}")
        return 1
    
    return 0


if __name__ == '__main__':
    exit(main())