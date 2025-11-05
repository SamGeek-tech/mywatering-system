using System;
using System.Threading.Tasks;
using Backend.Lib.Storage;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Azure.Devices;
using System.Text.Json;
using Microsoft.AspNetCore.SignalR;
using Backend.Api.Hubs;
using Backend.Lib.Model;
using Azure.Storage.Blobs;
using Microsoft.AspNetCore.Authorization;

namespace Backend.Api.Controllers
{
    [ApiController]
    [Route("api/devices")]
    public class DevicesController : ControllerBase
    {
        private readonly IStorage _storage;
        private readonly ServiceClient? _serviceClient;
        private readonly IHubContext<TelemetryHub>? _hubContext;

        public DevicesController(IStorage storage, IHubContext<TelemetryHub>? hubContext = null)
        {
            _storage = storage;
            _hubContext = hubContext;
            var conn = Environment.GetEnvironmentVariable("IOTHUB_CONNECTIONSTRING");
            if (!string.IsNullOrWhiteSpace(conn))
            {
                _serviceClient = ServiceClient.CreateFromConnectionString(conn);
            }
        }

        [HttpGet]
        public async Task<IActionResult> List()
        {
            var devices = await _storage.ListDevicesAsync();
            return Ok(devices);
        }

        [HttpGet("{id}/latest")]
        public async Task<IActionResult> Latest(string id)
        {
            var latest = await _storage.GetLatestAsync(id);
            if (latest == null) return NotFound();
            return Ok(latest);
        }

        [HttpGet("{id}/timeseries")]
        public async Task<IActionResult> Timeseries(string id, [FromQuery] DateTime? from, [FromQuery] DateTime? to)
        {
            var f = from ?? DateTime.UtcNow.AddHours(-1);
            var t = to ?? DateTime.UtcNow;
            var list = await _storage.QueryTimeseriesAsync(id, f, t);
            return Ok(list);
        }

        [Authorize(Policy = "RequireAuthenticated")]
        [HttpPost("{id}/command")]
        public async Task<IActionResult> Command(string id, [FromBody] object command)
        {
            if (_serviceClient == null) return StatusCode(503, "IoT Hub service client not configured");
            var msg = new Message(System.Text.Encoding.UTF8.GetBytes(JsonSerializer.Serialize(command)));
            try
            {
                await _serviceClient.SendAsync(id, msg);

                // Also notify realtime clients via SignalR if available
                if (_hubContext != null)
                {
                    await _hubContext.Clients.Group($"device:{id}").SendAsync("command", command);
                }

                return Ok();
            }
            catch (Exception ex)
            {
                return StatusCode(500, ex.Message);
            }
        }

        [Authorize(Policy = "RequireAuthenticated")]
        [HttpPost("{id}/ota")]
        public async Task<IActionResult> Ota(string id, [FromBody] OtaRequest ota)
        {
            if (ota == null || string.IsNullOrWhiteSpace(ota.Version) || string.IsNullOrWhiteSpace(ota.Url))
                return BadRequest("Invalid OTA request. 'version' and 'url' required.");

            // Upload OTA metadata to blob storage if configured
            var blobConn = Environment.GetEnvironmentVariable("RAW_BLOB_CONNECTIONSTRING");
            var otaContainer = Environment.GetEnvironmentVariable("OTA_BLOB_CONTAINER") ?? "ota";
            var metadataUrl = ota.Url;

            if (!string.IsNullOrWhiteSpace(blobConn))
            {
                try
                {
                    var blobService = new BlobServiceClient(blobConn);
                    var container = blobService.GetBlobContainerClient(otaContainer);
                    await container.CreateIfNotExistsAsync();
                    var name = $"ota/{id}/{ota.Version}/{DateTime.UtcNow:yyyyMMddHHmmss}.json";
                    var blob = container.GetBlobClient(name);
                    using var ms = new System.IO.MemoryStream(System.Text.Encoding.UTF8.GetBytes(JsonSerializer.Serialize(ota)));
                    await blob.UploadAsync(ms);
                    metadataUrl = blob.Uri.ToString();
                }
                catch (Exception ex)
                {
                    return StatusCode(500, "Failed to upload OTA metadata: " + ex.Message);
                }
            }

            // Send cloud-to-device message with OTA command
            if (_serviceClient == null) return StatusCode(503, "IoT Hub service client not configured");
            var payload = new { type = "ota", version = ota.Version, url = metadataUrl, checksum = ota.Checksum };
            var msg = new Message(System.Text.Encoding.UTF8.GetBytes(JsonSerializer.Serialize(payload)));
            try
            {
                await _serviceClient.SendAsync(id, msg);

                if (_hubContext != null)
                {
                    await _hubContext.Clients.Group($"device:{id}").SendAsync("ota", payload);
                }

                return Ok(new { status = "ok", otaUrl = metadataUrl });
            }
            catch (Exception ex)
            {
                return StatusCode(500, ex.Message);
            }
        }

        [HttpGet("{id}/ota/status")]
        public async Task<IActionResult> GetOtaStatus(string id)
        {
            var status = await _storage.GetOtaStatusAsync(id);
            if (status == null) return NotFound();
            return Ok(status);
        }

        [AllowAnonymous]
        [HttpPost("{id}/ota/status")]
        public async Task<IActionResult> ReportOtaStatus(string id, [FromBody] OtaStatus status)
        {
            if (status == null || string.IsNullOrWhiteSpace(status.State))
                return BadRequest("Invalid OTA status");

            status.Timestamp = DateTime.UtcNow;
            await _storage.SetOtaStatusAsync(id, status);

            if (_hubContext != null)
            {
                await _hubContext.Clients.Group($"device:{id}").SendAsync("otaStatus", status);
            }

            return Ok();
        }
    }
}
