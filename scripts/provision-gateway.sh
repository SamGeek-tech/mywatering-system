#!/usr/bin/env bash
# Provision a gateway device identity in IoT Hub and send a test telemetry message via Azure CLI
# Usage: ./scripts/provision-gateway.sh <hubName> <deviceId>
set -e
if [ "$#" -lt 2 ]; then
  echo "Usage: $0 <iothub-name> <gateway-device-id>"
  exit 1
fi
HUB=$1
DEVICE=$2

# Create device identity (symmetric key)
az iot hub device-identity create --hub-name $HUB --device-id $DEVICE || true

# Get device connection string
CONN=$(az iot hub device-identity connection-string show --hub-name $HUB --device-id $DEVICE --query connectionString -o tsv)
echo "Device connection string: $CONN"

# Send a test telemetry message to the IoT Hub using the Device SDK send method via az iot hub monitor-events is for events; instead use device telemetry via az iot device simulate? 
# We'll use az iot hub simulate-device (requires azure-cli-iot extension)
MSG='{"deviceId":"'$DEVICE'","timestamp":"'$(date -u +%Y-%m-%dT%H:%M:%SZ)'","sensors":[{"name":"moisture1","type":"capacitive","value":42.7,"unit":"%"}],"battery":3.72,"rssi":-72,"meshHopCount":1,"firmwareVersion":"1.0.0"}'

# ensure azure-cli-iot extension is installed
az extension add --name azure-iot || true

# Send message
az iot device simulate --device-id $DEVICE --hub-name $HUB --message "$MSG"

echo "Sent telemetry from $DEVICE to hub $HUB"