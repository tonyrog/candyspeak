#!/bin/bash
# Generate PDF from manual files

cd "$(dirname "$0")"

if ! command -v pandoc &> /dev/null; then
    echo "pandoc missing. Install with:"
    echo "  sudo apt install pandoc texlive-xetex fonts-dejavu"
    exit 1
fi

generate_pdf() {
    local src="$1"
    local dst="${src%.md}.pdf"

    echo "Generating $dst from $src..."
    pandoc "$src" -o "$dst" \
        --pdf-engine=xelatex \
        --toc \
        --toc-depth=2 \
        -V colorlinks=true \
        -V linkcolor=blue \
        -V urlcolor=blue \
        -V toccolor=black \
        -V mainfont="DejaVu Serif" \
        -V monofont="DejaVu Sans Mono"

    if [ $? -eq 0 ]; then
        echo "  OK: $dst"
        return 0
    else
        echo "  FAILED: $dst"
        return 1
    fi
}

# If specific file given as argument, process only that
if [ -n "$1" ]; then
    generate_pdf "$1"
    exit $?
fi

# Otherwise generate all manuals
errors=0

for manual in manual_en.md manual_sv.md; do
    if [ -f "$manual" ]; then
        generate_pdf "$manual" || ((errors++))
    fi
done

if [ $errors -eq 0 ]; then
    echo "All PDFs generated successfully"
else
    echo "$errors PDF(s) failed"
    exit 1
fi
