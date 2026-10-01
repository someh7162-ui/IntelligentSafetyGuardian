#!/bin/sh
set -eu
cd /home/teach/wt/riderguard
export RIDERGUARD_IMAGE_DIR=/home/teach/wt/riderguard/uploads
export RIDERGUARD_IMAGE_CLEANUP_ENABLED="${RIDERGUARD_IMAGE_CLEANUP_ENABLED:-true}"
export RIDERGUARD_IMAGE_RETENTION_DAYS="${RIDERGUARD_IMAGE_RETENTION_DAYS:-3}"
export RIDERGUARD_AI_URL=http://127.0.0.1:8091
export RIDERGUARD_AI_KEY="$(cat config/ai-key)"
export RIDERGUARD_DEMO_MODE=false
exec runtime/jre21/bin/java -Xms256m -Xmx1024m -jar app.jar --spring.profiles.active=prod --spring.config.additional-location=file:/home/teach/wt/riderguard/config/application.yml
