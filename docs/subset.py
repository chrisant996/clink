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

TMP_GLYPHS_PATH = ".build/docs/clink_tmp_glyphs.txt"
prompt_font = "CaskaydiaCoveNerdFontMono-Regular.woff2"


def main():
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

    prompt_path = os.path.join(docs_path, "prompts", prompt_font)
    assert os.path.exists(prompt_path)
    new_prompt_path = os.path.join(os.path.dirname(TMP_GLYPHS_PATH), prompt_font)

    try:
        subprocess.run(
            f"pyftsubset.exe {prompt_path} --unicodes-file={TMP_GLYPHS_PATH} "
            + f"--flavor=woff2 --output-file={new_prompt_path}",
            check=True,
        )
    except subprocess.CalledProcessError as e:
        print(f"Error {e.returncode} making a subset of the font file.\n {e.stderr}")

    print(f"Subset {prompt_font} from {os.path.getsize(prompt_path)/1000 :.2f} kB to {os.path.getsize(new_prompt_path)/1000 :.2f} kB")


if __name__ == "__main__":
    main()
