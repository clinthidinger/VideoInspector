#!/bin/bash
# Instagram Video Downloader for URL Logger
# This script extracts Instagram URLs from the log file and downloads videos using yt-dlp

# Check if yt-dlp is installed
if ! command -v yt-dlp &> /dev/null; then
    echo "Error: yt-dlp is not installed."
    echo "Please install it using: pip install yt-dlp"
    exit 1
fi

# Check for log file argument
if [ "$#" -lt 1 ]; then
    echo "Usage: $0 <url_log_file> [output_directory]"
    echo "Example: $0 url_log_2025-03-23.txt instagram_videos"
    exit 1
fi

LOG_FILE="$1"
OUTPUT_DIR="${2:-instagram_downloads}"

# Check if log file exists
if [ ! -f "$LOG_FILE" ]; then
    echo "Error: Log file '$LOG_FILE' not found."
    exit 1
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Extract Instagram URLs from the log file
echo "Extracting Instagram URLs from $LOG_FILE..."
INSTAGRAM_URLS=$(grep -E 'instagram.com/(p|reel)/' "$LOG_FILE" | awk -F ' - ' '{print $NF}')

# Count URLs
URL_COUNT=$(echo "$INSTAGRAM_URLS" | grep -c .)

if [ "$URL_COUNT" -eq 0 ]; then
    echo "No Instagram URLs found in the log file."
    exit 0
fi

echo "Found $URL_COUNT Instagram URLs."
echo "Starting downloads to $(realpath "$OUTPUT_DIR")..."

# Create timestamp for log file
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
LOG_PATH="$OUTPUT_DIR/download_log_$TIMESTAMP.txt"

# Initialize counters
SUCCESS_COUNT=0
FAILED_COUNT=0
FAILED_URLS=""

# Process each URL
COUNTER=1
while IFS= read -r URL; do
    if [ -z "$URL" ]; then
        continue
    fi
    
    echo -e "\n[$COUNTER/$URL_COUNT] Downloading: $URL"
    
    # Run yt-dlp
    if yt-dlp --no-warnings --restrict-filenames --no-playlist \
        -o "$OUTPUT_DIR/%(uploader)s_%(title)s_%(id)s.%(ext)s" "$URL"; then
        echo "✓ Successfully downloaded video from $URL"
        ((SUCCESS_COUNT++))
    else
        echo "✗ Failed to download $URL"
        FAILED_URLS="$FAILED_URLS$URL\n"
        ((FAILED_COUNT++))
    fi
    
    ((COUNTER++))
done <<< "$INSTAGRAM_URLS"

# Write log file
{
    echo "Instagram Download Results - $(date)"
    echo "Total URLs: $URL_COUNT"
    echo "Successfully Downloaded: $SUCCESS_COUNT"
    echo "Failed: $FAILED_COUNT"
    echo ""
    
    if [ "$FAILED_COUNT" -gt 0 ]; then
        echo "Failed URLs:"
        echo -e "$FAILED_URLS"
    fi
} > "$LOG_PATH"

# Display summary
echo -e "\nDownload Summary:"
echo "Total URLs: $URL_COUNT"
echo "Successfully Downloaded: $SUCCESS_COUNT"
echo "Failed: $FAILED_COUNT"
echo "Log saved to: $LOG_PATH"