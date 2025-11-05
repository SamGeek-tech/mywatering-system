#!/usr/bin/env bash
# Link DPS to IoT Hub after deployment. Usage: ./scripts/provision-dps-link.sh <dpsName> <resourceGroup> <iothubName>
set -e
if [ "$#" -lt 3 ]; then
  echo "Usage: $0 <dpsName> <resourceGroup> <iothubName>"
  exit 1
fi
DPS=$1
RG=$2
IOTHUB=$3

# Get IoT Hub connection string for the service policy (create if necessary)
az iot hub policy create --hub-name $IOTHUB --name servicePolicy --permissions RegistryRead ServiceConnect DeviceConnect || true
IOTHUB_CONN=$(az iot hub connection-string show --hub-name $IOTHUB --policy-name servicePolicy --query connectionString -o tsv)

# Link DPS to IoT Hub
az iot dps linked-hub create --dps-name $DPS --resource-group $RG --connection-string "$IOTHUB_CONN" --location $(az group show -n $RG --query location -o tsv) --name $IOTHUB

echo "Linked DPS $DPS to IoT Hub $IOTHUB"