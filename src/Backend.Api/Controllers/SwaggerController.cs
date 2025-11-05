using Microsoft.AspNetCore.Mvc;

namespace Backend.Api.Controllers
{
    [ApiController]
    [Route("/")]
    public class SwaggerController : ControllerBase
    {
        [HttpGet]
        public IActionResult Index() => Redirect("/swagger");
    }
}