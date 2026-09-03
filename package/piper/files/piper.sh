#!/bin/sh
# Wrapper: run the bundled piper with its own libs + espeak-ng data.
# Voices live in /usr/share/piper-voices or /opt/piper-voices.
exec env LD_LIBRARY_PATH="/opt/piper${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	/opt/piper/piper --espeak_data /opt/piper/espeak-ng-data "$@"
