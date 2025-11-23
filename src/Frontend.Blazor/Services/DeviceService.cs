using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Net.Http.Json;
using System.Threading.Tasks;
using Frontend.Blazor.Models;
using Microsoft.AspNetCore.SignalR.Client;

namespace Frontend.Blazor.Services
{
    public class DeviceService : IAsyncDisposable
    {
        private readonly HttpClient _http;
        private readonly HubConnection _hubConnection;
        
        public event Action<TelemetryPayload>? OnTelemetryReceived;

        public DeviceService(HttpClient http)
        {
            _http = http;
            
            // Assuming the backend is running on the same base URL or configured via HttpClient
            // For SignalR, we need the absolute URL if it's different, but if we use the same base, relative works.
            // However, typically HttpClient has a BaseAddress set.
            // Let's assume the hub is at /telemetryHub relative to the API base.
            
            var baseUrl = _http.BaseAddress?.ToString().TrimEnd('/') ?? "";
            // If BaseAddress is null, we might have an issue. But Blazor WASM usually sets it.
            
            _hubConnection = new HubConnectionBuilder()
                .WithUrl($"{baseUrl}/telemetryHub")
                .WithAutomaticReconnect()
                .Build();

            _hubConnection.On<TelemetryPayload>("telemetry", (payload) =>
            {
                OnTelemetryReceived?.Invoke(payload);
            });
            
            // Also listen for "command" or other events if needed, but user asked for data.
        }

        public async Task ConnectAsync()
        {
            if (_hubConnection.State == HubConnectionState.Disconnected)
            {
                await _hubConnection.StartAsync();
            }
        }

        public async Task<List<TelemetryPayload>> GetDevicesAsync()
        {
            return await _http.GetFromJsonAsync<List<TelemetryPayload>>("api/devices") ?? new List<TelemetryPayload>();
        }

        public async Task<List<TelemetryPayload>> GetTimeSeriesAsync(string deviceId, DateTime? from, DateTime? to)
        {
            var url = $"api/devices/{deviceId}/timeseries";
            var query = new List<string>();
            if (from.HasValue) query.Add($"from={from.Value:O}");
            if (to.HasValue) query.Add($"to={to.Value:O}");
            
            if (query.Count > 0)
            {
                url += "?" + string.Join("&", query);
            }

            return await _http.GetFromJsonAsync<List<TelemetryPayload>>(url) ?? new List<TelemetryPayload>();
        }
        
        public async Task JoinDeviceGroupAsync(string deviceId)
        {
             if (_hubConnection.State == HubConnectionState.Connected)
             {
                 await _hubConnection.InvokeAsync("JoinDeviceGroup", deviceId);
             }
        }

        public async ValueTask DisposeAsync()
        {
            if (_hubConnection is not null)
            {
                await _hubConnection.DisposeAsync();
            }
        }
    }
}
