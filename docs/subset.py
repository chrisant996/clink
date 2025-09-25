"""Script to extract all glyphs from prompt html and subset the promptFont

This script expects to be adjacent to the 'prompts' directory

Requires: fonttools brotli lxml

Current result (25/09.2025) - 100x decrease:
Subset CaskaydiaCoveNerdFontMono-Regular.woff2 from 1219.43 kB to 12.47 kB
"""

from lxml import etree
from glob import glob
import os
import subprocess
import sys

TMP_GLYPHS_PATH = ".build/docs/clink_tmp_glyphs.txt"


def main(src, dst):
    docs_path = os.path.dirname(__file__)

    parser = etree.HTMLParser(encoding="utf-8")
    all_text = ""
    for file in glob(os.path.join(docs_path, "prompts", "*.html")):
        filetree = etree.parse(file, parser)
        all_text += "".join(filetree.getroot().itertext())

    uniqs = set(all_text)
    unicode_points = []
    for char in uniqs:
        unicode_points.append(f"U+{ord(char):04X}")

    with open(TMP_GLYPHS_PATH, "w") as f:
        f.write(",".join(unicode_points))

    src_path = os.path.join(docs_path, "prompts", src)
    assert os.path.exists(src_path)
    dst_path = os.path.join(os.path.dirname(TMP_GLYPHS_PATH), dst)

    try:
        subprocess.run(
            f"pyftsubset.exe \"{src_path}\" --unicodes-file=\"{TMP_GLYPHS_PATH}\" "
            + f"--flavor=woff2 --output-file=\"{dst_path}\"",
            check=True,
        )
    except subprocess.CalledProcessError as e:
        print(f"Error {e.returncode} making a subset of the font file.\n {e.stderr}")

    print(f"Subset {src} from {os.path.getsize(src_path)/1000 :.2f} kB to {os.path.getsize(dst_path)/1000 :.2f} kB")


def usage():
    print("Usage:  python subset.py src_font_file dst_font_file")
    print("")
    print("Extracts glyphs from src_font_file needed for the prompts\\*.html files")
    print("and writes them to dst_font_file, producing a minimal subset of the file.")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        usage()
    else:
        main(sys.argv[1], sys.argv[2])
