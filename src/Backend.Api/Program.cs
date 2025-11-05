using Backend.Lib.Storage;
using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Identity.Web;

var builder = WebApplication.CreateBuilder(args);

var cosmosConn = builder.Configuration["CosmosConnectionString"];
if (!string.IsNullOrWhiteSpace(cosmosConn))
{
    builder.Services.AddSingleton<IStorage>(sp => new CosmosStorage(cosmosConn));
}
else
{
    builder.Services.AddSingleton<IStorage>(sp => new FileStorage());
}

builder.Services.AddControllers();
builder.Services.AddEndpointsApiExplorer();
builder.Services.AddSwaggerGen();

// Add SignalR for realtime websockets
builder.Services.AddSignalR();

// CORS for frontend
builder.Services.AddCors(options =>
{
    options.AddDefaultPolicy(policy =>
    {
        policy.AllowAnyOrigin().AllowAnyHeader().AllowAnyMethod();
    });
});

// Azure AD authentication (optional). Configure via appsettings or environment variables:
// AzureAd__Instance, AzureAd__TenantId, AzureAd__ClientId
var azureAdClientId = builder.Configuration["AzureAd:ClientId"]; // optional
if (!string.IsNullOrWhiteSpace(azureAdClientId))
{
    builder.Services.AddAuthentication("Bearer")
        .AddMicrosoftIdentityWebApi(builder.Configuration.GetSection("AzureAd"));

    builder.Services.AddAuthorization(options =>
    {
        options.AddPolicy("RequireAuthenticated", policy => policy.RequireAuthenticatedUser());
    });
}

var app = builder.Build();
if (app.Environment.IsDevelopment())
{
    app.UseSwagger();
    app.UseSwaggerUI();
}

app.UseCors();

if (!string.IsNullOrWhiteSpace(azureAdClientId))
{
    app.UseAuthentication();
    app.UseAuthorization();
}

app.UseStaticFiles();
app.MapControllers();
app.MapHub<Backend.Api.Hubs.TelemetryHub>("/telemetryHub");
app.Run();
