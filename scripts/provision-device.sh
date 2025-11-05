#!/usr/bin/env bash
# Provision a device in DPS and retrieve assigned IoT Hub using DPS registration via symmetric key enrollment
# Usage: ./scripts/provision-device.sh <dpsIdScope> <registrationId> <symmetricKey>
set -e
if [ "$#" -lt 3 ]; then
  echo "Usage: $0 <idScope> <registrationId> <symmetricKey>"
  exit 1
fi
IDSCOPE=$1
REGID=$2
SYMMETRIC_KEY=$3

# This script demonstrates how a device would register. For real ESP32 devices use the provisioning SDK.
# Here we simulate the registration using az cli (requires azure-cli-iot-ext)
az extension add --name azure-iot || true

# Create a temp file with registration payload
cat > payload.json <<EOF
{
  "registrationId": "$REGID",
  "payload": {
    "modelId": null
  }
}
EOF

# Note: az iot dps registration is not straightforward via az CLI; normally the device uses SDK.
# As an alternative, instruct the user to use DPS connection info on device to register.

echo "Device should use ID scope: $IDSCOPE and symmetric key to provision via DPS SDK."

echo "Simulation not implemented; use device SDK for provisioning."