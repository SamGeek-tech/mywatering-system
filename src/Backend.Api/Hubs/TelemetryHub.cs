using Microsoft.AspNetCore.SignalR;
using System.Threading.Tasks;

namespace Backend.Api.Hubs
{
    public class TelemetryHub : Hub
    {
        public Task JoinDeviceGroup(string deviceId)
        {
            return Groups.AddToGroupAsync(Context.ConnectionId, $"device:{deviceId}");
        }

        public Task LeaveDeviceGroup(string deviceId)
        {
            return Groups.RemoveFromGroupAsync(Context.ConnectionId, $"device:{deviceId}");
        }
    }
}