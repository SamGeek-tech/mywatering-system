using System;
using System.Text.Json;
using System.Threading.Tasks;
using Backend.Lib.Model;
using Backend.Lib.Storage;
using Microsoft.Azure.Functions.Worker;
using Microsoft.Extensions.Logging;
using Azure.Storage.Blobs;
using System.IO;
using Microsoft.Azure.Functions.Worker.Extensions.SignalRService;

namespace Backend.Functions.Functions
{
    public class IoTHubIngestFunction
    {
        private readonly IStorage _storage;
        private readonly ILogger _logger;

        public IoTHubIngestFunction(IStorage storage, ILoggerFactory loggerFactory)
        {
            _storage = storage ?? throw new ArgumentNullException(nameof(storage));
            _logger = loggerFactory.CreateLogger<IoTHubIngestFunction>();
        }

        [Function("IoTHubIngestFunction")]
        public async Task Run(
            [IoTHubTrigger("messages/events", Connection = "IoTHubConnectionString")] string message,
            [SignalR(HubName = "%SIGNALR_HUB%")] IAsyncCollector<SignalRMessage> signalRMessages)
        {
            try
            {
                _logger.LogInformation("IoT Hub message received: {len} bytes", message?.Length ?? 0);

                // Optional: Archive raw message to blob storage
                var blobConn = Environment.GetEnvironmentVariable("RAW_BLOB_CONNECTIONSTRING");
                var blobContainer = Environment.GetEnvironmentVariable("RAW_BLOB_CONTAINER") ?? "raw";
                if (!string.IsNullOrWhiteSpace(blobConn))
                {
                    try
                    {
                        var blobService = new BlobServiceClient(blobConn);
                        var container = blobService.GetBlobContainerClient(blobContainer);
                        await container.CreateIfNotExistsAsync();
                        var name = "raw/" + DateTime.UtcNow.ToString("yyyy/MM/dd/HHmmssfff") + "-message.json";
                        var blob = container.GetBlobClient(name);
                        using var ms = new MemoryStream(System.Text.Encoding.UTF8.GetBytes(message ?? ""));
                        await blob.UploadAsync(ms);
                    }
                    catch (Exception ex)
                    {
                        _logger.LogError(ex, "Failed to upload raw message to blob");
                    }
                }

                var payload = JsonSerializer.Deserialize<TelemetryPayload>(message);
                if (payload == null)
                {
                    _logger.LogWarning("Failed to deserialize telemetry payload");
                    return;
                }

                // Basic validation
                if (string.IsNullOrWhiteSpace(payload.DeviceId))
                {
                    _logger.LogWarning("Telemetry missing deviceId");
                    return;
                }

                // Ensure timestamp
                if (payload.Timestamp == default)
                    payload.Timestamp = DateTime.UtcNow;

                await _storage.InsertTimeseriesAsync(payload);
                await _storage.UpsertLatestAsync(payload);

                _logger.LogInformation("Stored telemetry for device {deviceId} at {ts}", payload.DeviceId, payload.Timestamp);

                // Broadcast to SignalR Service via output binding if available
                if (signalRMessages != null)
                {
                    try
                    {
                        var groupName = $"device:{payload.DeviceId}";
                        var msg = new SignalRMessage
                        {
                            GroupName = groupName,
                            Target = "telemetryReceived",
                            Arguments = new[] { payload }
                        };
                        await signalRMessages.AddAsync(msg);
                    }
                    catch (Exception ex)
                    {
                        _logger.LogError(ex, "Failed to publish to SignalR via binding");
                    }
                }

            }
            catch (Exception ex)
            {
                _logger.LogError(ex, "Error processing IoT Hub message");
            }
        }
    }
}}