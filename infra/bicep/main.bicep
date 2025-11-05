@description('Prefix for all resource names')
param prefix string = 'wateringpoc'
param location string = resourceGroup().location

resource storage 'Microsoft.Storage/storageAccounts@2022-09-01' = {
  name: toLower('${prefix}st')
  location: location
  sku: {
    name: 'Standard_RAGRS'
  }
  kind: 'StorageV2'
  properties: {
    accessTier: 'Hot'
  }
}

resource cosmos 'Microsoft.DocumentDB/databaseAccounts@2021-04-15' = {
  name: toLower('${prefix}-cosmos')
  location: location
  kind: 'GlobalDocumentDB'
  properties: {
    databaseAccountOfferType: 'Standard'
    locations: [
      {
        locationName: location
        failoverPriority: 0
        isZoneRedundant: false
      }
    ]
    consistencyPolicy: {
      defaultConsistencyLevel: 'Session'
    }
  }
}

resource signalr 'Microsoft.SignalRService/SignalR@2022-12-01' = {
  name: toLower('${prefix}-signalr')
  location: location
  sku: {
    name: 'Standard_S1'
    tier: 'Standard'
    capacity: 1
  }
  properties: {}
}

resource iothub 'Microsoft.Devices/IotHubs@2021-07-01' = {
  name: toLower('${prefix}-iothub')
  location: location
  sku: {
    name: 'S1'
    capacity: 1
  }
  properties: {
    eventHubEndpoints: {}
  }
}

// Device Provisioning Service (DPS) - create the service; link to IoT Hub via CLI after deployment
resource dps 'Microsoft.Devices/provisioningServices@2021-03-01' = {
  name: toLower('${prefix}-dps')
  location: location
  sku: {
    name: 'S1'
    capacity: 1
  }
  properties: {}
}

// User-assigned managed identity for Function App
resource functionIdentity 'Microsoft.ManagedIdentity/userAssignedIdentities@2018-11-30' = {
  name: '${prefix}-msi'
  location: location
}

resource functionAppPlan 'Microsoft.Web/serverfarms@2021-02-01' = {
  name: toLower('${prefix}-plan')
  location: location
  sku: {
    name: 'Y1'
    tier: 'Dynamic'
  }
}

resource functionApp 'Microsoft.Web/sites@2021-02-01' = {
  name: toLower('${prefix}-func')
  location: location
  kind: 'functionapp'
  identity: {
    type: 'UserAssigned'
    userAssignedIdentities: {
      '${functionIdentity.id}': {}
    }
  }
  properties: {
    serverFarmId: functionAppPlan.id
    siteConfig: {
      appSettings: [
        {
          name: 'FUNCTIONS_WORKER_RUNTIME'
          value: 'dotnet-isolated'
        }
        {
          name: 'AzureWebJobsStorage'
          // will be set post-deploy to Key Vault reference
          value: ''
        }
        {
          name: 'RAW_BLOB_CONTAINER'
          value: 'raw'
        }
        {
          name: 'OTA_BLOB_CONTAINER'
          value: 'ota'
        }
        {
          name: 'SIGNALR_HUB'
          value: 'telemetry'
        }
      ]
    }
  }
}

// Key Vault to store connection strings
resource keyVault 'Microsoft.KeyVault/vaults@2021-06-01-preview' = {
  name: toLower('${prefix}-kv')
  location: location
  properties: {
    sku: {
      family: 'A'
      name: 'standard'
    }
    tenantId: subscription().tenantId
    accessPolicies: [
      {
        tenantId: subscription().tenantId
        objectId: functionIdentity.principalId
        permissions: {
          // Limit to get/list only for runtime access; secrets are created by the deployment itself.
          secrets: [ 'get', 'list' ]
        }
      }
    ]
    enabledForDeployment: true
    enableSoftDelete: true
  }
}

// Obtain keys and construct connection strings
var storageKeys = listKeys(storage.id, '2022-09-01')
var storageConnString = 'DefaultEndpointsProtocol=https;AccountName=' + storage.name + ';AccountKey=' + storageKeys.keys[0].value + ';EndpointSuffix=core.windows.net'

var cosmosKeys = listKeys(cosmos.id, '2021-04-15')
var cosmosEndpoint = cosmos.properties.documentEndpoint
var cosmosConnString = 'AccountEndpoint=' + cosmosEndpoint + ';AccountKey=' + cosmosKeys.primaryMasterKey

var signalrKeys = listKeys(signalr.id, '2022-12-01')
var signalrConnString = 'Endpoint=' + signalr.properties.hostName + ';AccessKey=' + signalrKeys.primaryKey + ';Version=1.0'

// Create Key Vault secrets for the connection strings
resource secretStorage 'Microsoft.KeyVault/vaults/secrets@2021-06-01-preview' = {
  name: '${keyVault.name}/storage-conn'
  properties: {
    value: storageConnString
  }
  dependsOn: [ keyVault ]
}

resource secretCosmos 'Microsoft.KeyVault/vaults/secrets@2021-06-01-preview' = {
  name: '${keyVault.name}/cosmos-conn'
  properties: {
    value: cosmosConnString
  }
  dependsOn: [ keyVault ]
}

resource secretSignalr 'Microsoft.KeyVault/vaults/secrets@2021-06-01-preview' = {
  name: '${keyVault.name}/signalr-conn'
  properties: {
    value: signalrConnString
  }
  dependsOn: [ keyVault ]
}

// Note: IoT Hub connection string requires a policy and is created post-deploy via CLI; handled in deployment workflow.
// DPS linking to IoT Hub should also be performed post-deploy using 'az iot dps linked-hub create' with proper keys.

// Outputs
output storageAccountName string = storage.name
output storageConnectionString string = storageConnString
output cosmosName string = cosmos.name
output cosmosConnectionString string = cosmosConnString
output signalrName string = signalr.name
output signalrConnectionString string = signalrConnString
output iothubName string = iothub.name
output iothubHostName string = iothub.properties.hostName
output functionAppName string = functionApp.name
output functionIdentityClientId string = functionIdentity.clientId
output functionIdentityPrincipalId string = functionIdentity.principalId
output keyVaultName string = keyVault.name
output secretStorageId string = secretStorage.id
output secretCosmosId string = secretCosmos.id
output secretSignalrId string = secretSignalr.id
output dpsName string = dps.name

// Recommend: after deployment, use the deployment workflow to assign roles and configure Function App settings with Key Vault references, create DPS linked hub, and create IoT Hub policy for service access.
