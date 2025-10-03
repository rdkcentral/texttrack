#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
#
# If not stated otherwise in this file or this component's LICENSE
# file the following copyright and licenses apply:
#
# Copyright 2024 Comcast Cable Communications Management, LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
DISPLAY=""
CC=0
TTML=0
WEBVTT=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    -d|--display)
      shift
      DISPLAY="$1"
      shift
      ;;
    CC)
      shift
      CC=1
      ;;
    TTML)
      shift
      TTML=1
      ;;
    WEBVTT)
      shift
      WEBVTT=1
      ;;
    -*)
      echo "Unknown option '$1'"
      exit 1;
      ;;
    *)
      echo "Unknown argument '$1'"
      exit 1
      ;;
  esac
done
if [ -z "$DISPLAY" ]; then
  echo "No -d|--display option given"
  exit 1
fi
if [ "$CC$TTML$WEBVTT" = "000" ]; then
  CC=1
  TTML=1
  WEBVTT=1
fi
jsonrpc_id=0
function jsonrpc() {
  jsonrpc_id=$((jsonrpc_id + 1))
  result=$(curl -s -X POST -H "Content-Type: application/json" http://127.0.0.1:9998/jsonrpc -d '{"jsonrpc":"2.0","id":'$jsonrpc_id',"method":"org.rdk.TextTrack.1.'$1'","params":'"$2"'}')
  if grep -q '"error":' <<< "$result"; then
    echo "Request: "'{"jsonrpc":"2.0","id":'$jsonrpc_id',"method":"org.rdk.TextTrack.1.'$1'","params":'"$2"'}'
    echo "Result: $result"
    exit 1
  fi
  if [ -n "$3" ]; then
    echo $result
  fi
}
echo "Opening session on $DISPLAY"
out=$(jsonrpc openSession '{"displayHandle":"'${DISPLAY}'"}' ECHO)
sessionId=$(<<< "$out" tr -d '{}' |sed -E 's/"?,/\n/g' | awk 'BEGIN{FS=":"} /"result"/{print $2}')
if [ -z "$sessionId" ]; then
  echo "Could not parse sessionId from response: $out"
  exit 1
fi
echo "Session ID is ${sessionId}"
trap 'jsonrpc closeSession {\"sessionId\":${sessionId}}' EXIT
if [ $CC = 1 ]; then
  echo "*** Testing CC"
  jsonrpc setSessionClosedCaptionsService '{"sessionId":'${sessionId}',"service":"CC1"}'
  echo "Unmute"
  jsonrpc unMuteSession '{"sessionId":'${sessionId}'}'
  echo "Reset style"
  jsonrpc setClosedCaptionsStyle '{"style":{"fontFamily":"CONTENT_DEFAULT","fontSize":"CONTENT_DEFAULT","fontColor":"","fontOpacity":-1,"fontEdge":"CONTENT_DEFAULT","fontEdgeColor":"","backgroundColor":"","backgroundOpacity":-1,"windowColor":"","windowOpacity":-1}}'
  jsonrpc setPreviewText '{"sessionId":'${sessionId}', "text":"Closed Captions Settings Preview Text"}'
  sleep 2
  for x in "#ff0000 Red" "#00ff00 Green" "#0000ff Blue"; do
    read color text <<< $x
    echo "Change font color to $text"
    jsonrpc setFontColor '{"color":"'$color'"}'
    sleep 2
  done
  echo "Reset style"
  jsonrpc setClosedCaptionsStyle '{"style":{"fontFamily":"CONTENT_DEFAULT","fontSize":"CONTENT_DEFAULT","fontColor":"","fontOpacity":-1,"fontEdge":"CONTENT_DEFAULT","fontEdgeColor":"","backgroundColor":"","backgroundOpacity":-1,"windowColor":"","windowOpacity":-1}}'
  for x in SMALL REGULAR LARGE EXTRA_LARGE; do
    echo "Change font size to $x"
    jsonrpc setFontSize '{"size":"'$x'"}'
    sleep 2
  done
  echo "Reset style"
  jsonrpc setClosedCaptionsStyle '{"style":{"fontFamily":"CONTENT_DEFAULT","fontSize":"CONTENT_DEFAULT","fontColor":"","fontOpacity":-1,"fontEdge":"CONTENT_DEFAULT","fontEdgeColor":"","backgroundColor":"","backgroundOpacity":-1,"windowColor":"","windowOpacity":-1}}'
  sleep 1
  echo "Mute"
  jsonrpc muteSession '{"sessionId":'${sessionId}'}'
  echo "Testing CC - done"
fi
if [ $TTML = 1 ]; then
  echo "*** Testing TTML"
  jsonrpc setSessionTTMLSelection '{"sessionId":'${sessionId}'}'
  echo "Unmute"
  jsonrpc unMuteSession '{"sessionId":'${sessionId}'}'
  echo "Set time to 0"
  jsonrpc sendSessionTimestamp '{"sessionId":'${sessionId}',"mediaTimestampMs":0}'
TTMLCONTENT=$(sed -e 's/"/\\"/g; s/\n//g;' <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<tt xmlns="http://www.w3.org/ns/ttml" xmlns:tts="http://www.w3.org/ns/ttml#styling" xmlns:ttp="http://www.w3.org/ns/ttml#parameter">
<body>
 <span>
  <p begin="00:00:00.500" end="00:00:02">TTML First line for 1.5 seconds</p>
  <p begin="00:00:02.500" end="00:00:05">TTML Second line for 2.5 seconds</p>
  <p begin="00:00:05.100" end="00:00:05.900" tts:textAlign="left">TTML Left</p>
  <p begin="00:00:06.100" end="00:00:06.900" tts:textAlign="center">TTML Center</p>
  <p begin="00:00:07.100" end="00:00:07.900" tts:textAlign="right">TTML Right</p>
 </span>
</body>
</tt>
EOF)
  echo "Send data"
  jsonrpc sendSessionData '{"sessionId":'${sessionId}',"type":"TTML","displayOffsetMs":0,"data":"'"$(<<< $TTMLCONTENT tr -d '\n')"'"}'
  sleep 4
  echo "Send style and more data"
  jsonrpc applyCustomTtmlStyleOverridesToSession '{"sessionId":'${sessionId}',"style":"color:teal;backgroundColor:white"}'
TTMLCONTENT=$(sed -e 's/"/\\"/g' <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<tt xmlns="http://www.w3.org/ns/ttml" xmlns:tts="http://www.w3.org/ns/ttml#styling" xmlns:ttp="http://www.w3.org/ns/ttml#parameter">
<body>
 <span>
  <p begin="00:00:12" end="00:00:15">TTML style override: Teal on White</p>
 </span>
</body>
</tt>
EOF)
  # Use offset to get it displayed at time 8.5
  jsonrpc sendSessionData '{"sessionId":'${sessionId}',"type":"TTML","displayOffsetMs":-3500,"data":"'"$(<<< $TTMLCONTENT tr -d '\n')"'"}'
  sleep 9
  echo "Mute"
  jsonrpc muteSession '{"sessionId":'${sessionId}'}'
  echo "Testing TTML - done"
fi
if [ $WEBVTT = 1 ]; then
  echo "*** Testing WebVTT"
  jsonrpc setSessionWebVTTSelection '{"sessionId":'${sessionId}'}'
  echo "Unmute"
  jsonrpc unMuteSession '{"sessionId":'${sessionId}'}'
  echo "Set time to 0"
  jsonrpc sendSessionTimestamp '{"sessionId":'${sessionId}',"mediaTimestampMs":0}'
  echo "Send data"
  # Check behaviour of displayOffsetMs
  jsonrpc sendSessionData '{"sessionId":'${sessionId}',"type":"WEBVTT","displayOffsetMs":3000,"data":"WEBVTT\nX-TIMESTAMP-MAP=MPEGTS:0\n00:00:00.500 --> 00:00:02.000\nWebVTT Second line for 1.5 seconds\n\n"}'
  jsonrpc sendSessionData '{"sessionId":'${sessionId}',"type":"WEBVTT","displayOffsetMs":-2000,"data":"WEBVTT\nX-TIMESTAMP-MAP=MPEGTS:0\n00:00:02.500 --> 00:00:05.000\nWebVTT First line for 2.5 seconds\n"}'
  jsonrpc sendSessionData '{"sessionId":'${sessionId}',"type":"WEBVTT","displayOffsetMs":0,"data":"WEBVTT\n00:00:05.100 --> 00:00:05.700\nWebVTT ends\n"}'
  sleep 6
  echo "Mute"
  jsonrpc muteSession '{"sessionId":'${sessionId}'}'
  echo "Testing WebVTT - done"
fi
