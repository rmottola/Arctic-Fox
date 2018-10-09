# Usage: sh update.sh <upstream_src_directory>
set -e

if [ ! -d "$1" ]; then
  echo "Usage: ./update.sh /path/to/kiss_fft" > 2
  exit 1
fi

FILES="docs/CHANGELOG \
       LICENSE \
       README \
       docs/README.simd \
       docs/BACKGROUND \
       _kiss_fft_guts.h \
       kiss_fft.c \
       kiss_fft.h \
       tools/kiss_fftr.c \
       tools/kiss_fftr.h"

for file in $FILES; do
  cp "$1/$file" .
done

echo "Remember to update README_MOZILLA with the version details."

