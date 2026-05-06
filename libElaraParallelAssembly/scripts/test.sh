#!/usr/bin/env bash
set -e

if [ $# -ne 2 ]; then
  echo "Usage: $0 <server_url> <blob_file>"
  echo "Example: $0 http://localhost:8080 model.epa0"
  exit 1
fi

SERVER="$1"
BLOB="$2"

echo "Submitting EPA job..."
RESP=$(curl -s -X POST \
  -H "Content-Type: application/octet-stream" \
  --data-binary @"$BLOB" \
  "$SERVER/jobs")

echo "Response: $RESP"

# Extract UUID from {"id":"..."}
JOB_ID=$(echo "$RESP" | sed -n 's/.*"id"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p')

if [ -z "$JOB_ID" ]; then
  echo "Failed to extract job id"
  exit 1
fi

echo "Job ID: $JOB_ID"

echo "Sleeping 5 seconds..."
sleep 5

echo "Checking job status..."
STATUS=$(curl -s "$SERVER/jobs/$JOB_ID")

echo "Status: $STATUS"

# If still not done, poll until done or error
while true; do
  if echo "$STATUS" | grep -q '"status":"done"'; then
    echo "Job done."
    break
  fi
  if echo "$STATUS" | grep -q '"status":"error"'; then
    echo "Job failed:"
    echo "$STATUS"
    exit 1
  fi
  echo "Not ready yet, sleeping 2s..."
  sleep 2
  STATUS=$(curl -s "$SERVER/jobs/$JOB_ID")
done

OUT="result_${JOB_ID}.bin"
echo "Fetching result -> $OUT"

curl -s -o "$OUT" "$SERVER/jobs/$JOB_ID/result"

echo "Done."
echo "Result saved to $OUT"
