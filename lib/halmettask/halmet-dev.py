#!/usr/bin/env python3
"""
ESP32-NMEA2000 Development Server

A smart dev tool that:
1. Uses PlatformIO's build system to compile web assets (maximum compatibility)
2. Watches JS/CSS source files for changes and triggers rebuilds
3. Proxies API calls and WebSocket connections to the ESP32

Usage:
    ./tools/halmet-dev.py <esp32-ip-or-hostname> [environment] [port]

    Examples:
        ./tools/halmet-dev.py 192.168.1.100
        ./tools/halmet-dev.py halmet.local halmet
        ./tools/halmet-dev.py 192.168.1.100 halmet 3000

Then open http://localhost:8080 in your browser.
Edit your JS/CSS files, save, and just refresh the browser!

Requirements:
    pip install aiohttp
    PlatformIO CLI (pio) must be in PATH
"""

import os
import sys
import subprocess
import time
import asyncio
import glob
import configparser
from pathlib import Path

try:
    import aiohttp
    from aiohttp import web, ClientSession, WSMsgType
except ImportError:
    print("Error: aiohttp is required for WebSocket support")
    print("Install with: pip install aiohttp")
    sys.exit(1)

# Output directory
GEN_DIR = 'lib/generated'

# Server port
DEFAULT_PORT = 8080
DEFAULT_ENV = 'halmet'


class Colors:
    """ANSI color codes for pretty output"""
    RESET = '\033[0m'
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    BOLD = '\033[1m'


def log(msg, color=None):
    """Print a timestamped log message"""
    timestamp = time.strftime('%H:%M:%S')
    if color:
        print(f"{Colors.CYAN}[{timestamp}]{Colors.RESET} {color}{msg}{Colors.RESET}")
    else:
        print(f"{Colors.CYAN}[{timestamp}]{Colors.RESET} {msg}")


def log_success(msg):
    log(msg, Colors.GREEN)


def log_warning(msg):
    log(msg, Colors.YELLOW)


def log_error(msg):
    log(msg, Colors.RED)


def log_info(msg):
    log(msg, Colors.BLUE)


def get_user_task_dirs(base_dir):
    """Find all task directories (matches lib/*task*)"""
    return glob.glob(os.path.join(base_dir, 'lib', '*task*'))


def parse_platformio_ini(base_dir, env_name):
    """
    Parse platformio.ini and extract custom_js and custom_css for a given environment.
    Follows the same logic as extra_script.py
    """
    ini_files = [
        os.path.join(base_dir, 'platformio.ini'),
    ]
    
    # Also check task-specific platformio.ini files
    for task_dir in get_user_task_dirs(base_dir):
        task_ini = os.path.join(task_dir, 'platformio.ini')
        if os.path.exists(task_ini):
            ini_files.append(task_ini)
    
    config = configparser.ConfigParser()
    # Preserve case of option names
    config.optionxform = str
    
    for ini_file in ini_files:
        if os.path.exists(ini_file):
            try:
                config.read(ini_file)
            except Exception as e:
                log_warning(f"Could not parse {ini_file}: {e}")
    
    # Look for the environment section
    env_section = f'env:{env_name}'
    
    custom_js = []
    custom_css = []
    
    if config.has_section(env_section):
        if config.has_option(env_section, 'custom_js'):
            raw = config.get(env_section, 'custom_js')
            # Handle both comma-separated and newline-separated
            sep = '\n' if '\n' in raw else ','
            custom_js = [f.strip() for f in raw.split(sep) if f.strip()]
        
        if config.has_option(env_section, 'custom_css'):
            raw = config.get(env_section, 'custom_css')
            sep = '\n' if '\n' in raw else ','
            custom_css = [f.strip() for f in raw.split(sep) if f.strip()]
    
    return custom_js, custom_css


def discover_web_files(base_dir, env_name):
    """
    Discover all JS/CSS files using the same logic as extra_script.py:
    1. Core files (web/index.js, web/index.css)
    2. Task directories (lib/*task*/index.js, lib/*task*/index.css)
    3. custom_js/custom_css from platformio.ini
    """
    js_files = ['web/index.js']
    css_files = ['web/index.css']
    
    # Get task directories and check for index.js/index.css
    task_dirs = get_user_task_dirs(base_dir)
    for task_dir in task_dirs:
        rel_dir = os.path.relpath(task_dir, base_dir)
        
        index_js = os.path.join(task_dir, 'index.js')
        if os.path.exists(index_js):
            js_files.append(os.path.join(rel_dir, 'index.js'))
        
        index_css = os.path.join(task_dir, 'index.css')
        if os.path.exists(index_css):
            css_files.append(os.path.join(rel_dir, 'index.css'))
    
    # Get custom files from platformio.ini
    custom_js, custom_css = parse_platformio_ini(base_dir, env_name)
    js_files.extend(custom_js)
    css_files.extend(custom_css)
    
    # Remove duplicates while preserving order
    seen_js = set()
    unique_js = []
    for f in js_files:
        if f not in seen_js:
            seen_js.add(f)
            unique_js.append(f)
    
    seen_css = set()
    unique_css = []
    for f in css_files:
        if f not in seen_css:
            seen_css.add(f)
            unique_css.append(f)
    
    return unique_js, unique_css


class WebAssetBuilder:
    """Handles building web assets using PlatformIO's build system"""
    
    def __init__(self, base_dir, env_name):
        self.base_dir = Path(base_dir)
        self.gen_dir = self.base_dir / GEN_DIR
        self.env_name = env_name
        
        # Discover files to watch (for change detection)
        self.js_files, self.css_files = discover_web_files(str(base_dir), env_name)
        
        # Track file modification times
        self.mtimes = {}
        self._update_mtimes()
    
    def _update_mtimes(self):
        """Update stored modification times for all watched files"""
        for f in self.js_files + self.css_files:
            path = self.base_dir / f
            if path.exists():
                self.mtimes[f] = path.stat().st_mtime
    
    def _run_pio_build(self):
        """
        Run PlatformIO build with compiledb target.
        This triggers extra_script.py's prebuild() which handles:
        - Merging config.json files
        - Joining and gzipping JS/CSS files
        - Generating embedded file headers
        - All with proper caching (only rebuilds changed files)
        """
        log_info(f"Running PlatformIO prebuild for '{self.env_name}'...")
        
        try:
            result = subprocess.run(
                ['pio', 'run', '-e', self.env_name, '-t', 'compiledb'],
                cwd=str(self.base_dir),
                capture_output=True,
                text=True,
                timeout=120
            )
            
            # Show relevant output (filter for prebuild messages)
            for line in result.stdout.split('\n'):
                if any(x in line for x in ['#prebuild', 'creating', 'adding', 'is up to date', 'is newer']):
                    # Color code the output
                    if 'creating' in line or 'adding' in line:
                        log(f"  {line.strip()}", Colors.GREEN)
                    elif 'up to date' in line or 'newer' in line:
                        log(f"  {line.strip()}", Colors.BLUE)
                    else:
                        log(f"  {line.strip()}")
            
            if result.returncode != 0:
                log_error("PlatformIO build failed!")
                for line in result.stderr.split('\n')[-10:]:
                    if line.strip():
                        log_error(f"  {line}")
                return False
            
            log_success("Build complete")
            return True
            
        except FileNotFoundError:
            log_error("'pio' command not found. Is PlatformIO CLI installed?")
            log_error("Install with: pip install platformio")
            return False
        except subprocess.TimeoutExpired:
            log_error("Build timed out after 120 seconds")
            return False
    
    def build_all(self):
        """Build all web assets using PlatformIO"""
        success = self._run_pio_build()
        self._update_mtimes()
        return success
    
    def check_and_rebuild(self):
        """Check for file changes and rebuild if needed"""
        changed_files = []
        
        for f in self.js_files + self.css_files:
            path = self.base_dir / f
            if path.exists():
                mtime = path.stat().st_mtime
                if f not in self.mtimes or mtime > self.mtimes[f]:
                    changed_files.append(f)
        
        if changed_files:
            log_warning(f"Detected changes in: {', '.join(Path(f).name for f in changed_files)}")
            success = self._run_pio_build()
            self._update_mtimes()
            return success
        
        return False


class DevServer:
    """Development server with HTTP and WebSocket proxying"""
    
    def __init__(self, base_dir, esp32_host, port, env_name):
        self.base_dir = Path(base_dir)
        self.esp32_host = esp32_host
        self.port = port
        self.env_name = env_name
        self.builder = WebAssetBuilder(base_dir, env_name)
        self.app = web.Application()
        self._client_session = None  # Reusable session for API calls
        self._setup_routes()
        
        # Setup startup/cleanup handlers
        self.app.on_startup.append(self._on_startup)
        self.app.on_cleanup.append(self._on_cleanup)
    
    async def _on_startup(self, app):
        """Create shared client session on startup"""
        timeout = aiohttp.ClientTimeout(total=10, connect=5)
        self._client_session = ClientSession(timeout=timeout)
    
    async def _on_cleanup(self, app):
        """Cleanup client session on shutdown"""
        if self._client_session:
            await self._client_session.close()
    
    def _setup_routes(self):
        """Configure URL routes"""
        self.app.router.add_get('/ws', self._handle_websocket)
        self.app.router.add_route('*', '/api/{path:.*}', self._handle_api_proxy)
        self.app.router.add_get('/{path:.*}', self._handle_static)
    
    async def _handle_websocket(self, request):
        """Proxy WebSocket connections to ESP32 with proper cleanup"""
        ws_server = web.WebSocketResponse(heartbeat=30)  # Keep-alive ping every 30s
        await ws_server.prepare(request)
        
        esp32_ws_url = f'ws://{self.esp32_host}/ws'
        log_info(f"WebSocket: connecting to {esp32_ws_url}")
        
        ws_client = None
        forward_tasks = []
        
        try:
            # Short timeout for initial connection
            timeout = aiohttp.ClientTimeout(total=10, connect=5)
            session = ClientSession(timeout=timeout)
            
            try:
                ws_client = await session.ws_connect(
                    esp32_ws_url,
                    heartbeat=30,  # Ping ESP32 every 30s to detect dead connections
                    timeout=10
                )
                log_success("WebSocket: connected to ESP32")
                
                async def forward_to_client():
                    """Forward messages from ESP32 to browser"""
                    try:
                        async for msg in ws_client:
                            if ws_server.closed:
                                break
                            if msg.type == WSMsgType.TEXT:
                                await ws_server.send_str(msg.data)
                            elif msg.type == WSMsgType.BINARY:
                                await ws_server.send_bytes(msg.data)
                            elif msg.type in (WSMsgType.ERROR, WSMsgType.CLOSED):
                                break
                    except Exception as e:
                        log_warning(f"WebSocket forward_to_client error: {e}")
                
                async def forward_to_esp32():
                    """Forward messages from browser to ESP32"""
                    try:
                        async for msg in ws_server:
                            if ws_client.closed:
                                break
                            if msg.type == WSMsgType.TEXT:
                                await ws_client.send_str(msg.data)
                            elif msg.type == WSMsgType.BINARY:
                                await ws_client.send_bytes(msg.data)
                            elif msg.type in (WSMsgType.ERROR, WSMsgType.CLOSED):
                                break
                    except Exception as e:
                        log_warning(f"WebSocket forward_to_esp32 error: {e}")
                
                # Run both directions concurrently, cancel both when either finishes
                forward_tasks = [
                    asyncio.create_task(forward_to_client()),
                    asyncio.create_task(forward_to_esp32())
                ]
                
                # Wait for either task to complete (connection closed from either side)
                done, pending = await asyncio.wait(
                    forward_tasks,
                    return_when=asyncio.FIRST_COMPLETED
                )
                
                # Cancel pending tasks
                for task in pending:
                    task.cancel()
                    try:
                        await task
                    except asyncio.CancelledError:
                        pass
                        
            finally:
                # Always close the WebSocket client and session
                if ws_client and not ws_client.closed:
                    await ws_client.close()
                await session.close()
                
        except asyncio.TimeoutError:
            log_error("WebSocket: connection to ESP32 timed out")
        except Exception as e:
            log_error(f"WebSocket error: {e}")
        finally:
            # Cancel any remaining tasks
            for task in forward_tasks:
                if not task.done():
                    task.cancel()
            
            # Close server-side WebSocket
            if not ws_server.closed:
                await ws_server.close()
            log_info("WebSocket: disconnected")
        
        return ws_server
    
    async def _handle_api_proxy(self, request):
        """Proxy API requests to ESP32 using shared session"""
        path = request.match_info['path']
        url = f'http://{self.esp32_host}/api/{path}'
        
        log(f"→ API: /{path}", Colors.YELLOW)
        
        try:
            # Forward headers except host
            headers = {k: v for k, v in request.headers.items() 
                      if k.lower() not in ('host', 'accept-encoding')}
            
            body = await request.read()
            
            async with self._client_session.request(
                request.method, url, 
                headers=headers, 
                data=body if body else None
            ) as resp:
                response_body = await resp.read()
                return web.Response(
                    body=response_body,
                    status=resp.status,
                    headers={k: v for k, v in resp.headers.items()
                            if k.lower() not in ('transfer-encoding', 'content-encoding')}
                )
        except asyncio.TimeoutError:
            log_error(f"API proxy timeout: /{path}")
            return web.Response(
                text='{"error": "Gateway timeout"}',
                status=504,
                content_type='application/json'
            )
        except Exception as e:
            log_error(f"API proxy error: {e}")
            return web.Response(
                text=f'{{"error": "Bad gateway: {str(e)}"}}',
                status=502,
                content_type='application/json'
            )
    
    async def _handle_static(self, request):
        """Serve static files with auto-rebuild"""
        # Check for file changes
        self.builder.check_and_rebuild()
        
        path = request.match_info['path']
        if not path or path == '/':
            path = 'index.html'
        
        # Try lib/generated first, then web/
        for base in [self.base_dir / 'lib' / 'generated', self.base_dir / 'web']:
            file_path = base / path
            
            # Try exact path
            if file_path.is_file():
                return self._serve_file(file_path)
            
            # Try .gz version
            gz_path = base / (path + '.gz')
            if gz_path.is_file():
                return self._serve_file(gz_path, is_gzip=True, original_name=path)
        
        return web.Response(text="Not found", status=404)
    
    def _serve_file(self, file_path, is_gzip=False, original_name=None):
        """Serve a file with appropriate content type"""
        name = original_name or file_path.name
        if name.endswith('.gz'):
            name = name[:-3]
        
        # Guess content type
        content_types = {
            '.html': 'text/html',
            '.js': 'application/javascript',
            '.css': 'text/css',
            '.json': 'application/json',
            '.png': 'image/png',
            '.jpg': 'image/jpeg',
            '.gif': 'image/gif',
            '.svg': 'image/svg+xml',
            '.ico': 'image/x-icon',
        }
        
        ext = Path(name).suffix.lower()
        content_type = content_types.get(ext, 'application/octet-stream')
        
        with open(file_path, 'rb') as f:
            body = f.read()
        
        headers = {}
        if is_gzip:
            headers['Content-Encoding'] = 'gzip'
        
        log(f"← {name}" + (" (gzip)" if is_gzip else ""), Colors.GREEN)
        
        return web.Response(body=body, content_type=content_type, headers=headers)
    
    def run(self):
        """Start the server"""
        # Initial build using PlatformIO
        log_info(f"Building web assets for environment '{self.env_name}'...")
        if not self.builder.build_all():
            log_error("Initial build failed, continuing anyway...")
        
        print()
        print(f"{Colors.BOLD}{'=' * 60}{Colors.RESET}")
        print(f"{Colors.GREEN}{Colors.BOLD}  ESP32-NMEA2000 Development Server{Colors.RESET}")
        print(f"{Colors.BOLD}{'=' * 60}{Colors.RESET}")
        print()
        print(f"  {Colors.CYAN}Environment:{Colors.RESET} {self.env_name}")
        print(f"  {Colors.CYAN}Local:{Colors.RESET}       http://localhost:{self.port}")
        print(f"  {Colors.CYAN}API proxy:{Colors.RESET}   http://{self.esp32_host}/api")
        print(f"  {Colors.CYAN}WS proxy:{Colors.RESET}    ws://{self.esp32_host}/ws")
        print()
        print(f"  {Colors.YELLOW}Watching for changes in:{Colors.RESET}")
        for f in self.builder.js_files + self.builder.css_files:
            print(f"    • {f}")
        print()
        print(f"  {Colors.BLUE}Using PlatformIO build system for maximum compatibility{Colors.RESET}")
        print(f"  {Colors.GREEN}Save a file and refresh your browser!{Colors.RESET}")
        print(f"{Colors.BOLD}{'=' * 60}{Colors.RESET}")
        print()
        
        web.run_app(self.app, host='0.0.0.0', port=self.port, print=None)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        print(f"Error: Missing ESP32 address")
        print(f"Usage: {sys.argv[0]} <esp32-ip-or-hostname> [environment] [port]")
        print(f"       Default environment: {DEFAULT_ENV}")
        print(f"       Default port: {DEFAULT_PORT}")
        sys.exit(1)
    
    esp32_addr = sys.argv[1]
    env_name = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_ENV
    port = int(sys.argv[3]) if len(sys.argv) > 3 else DEFAULT_PORT
    
    # Find project root
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    
    # Verify we're in the right place
    if not os.path.exists(os.path.join(base_dir, 'platformio.ini')):
        log_error(f"Cannot find platformio.ini in {base_dir}")
        log_error("Please run this script from the project root or tools/ directory")
        sys.exit(1)
    
    server = DevServer(base_dir, esp32_addr, port, env_name)
    
    try:
        server.run()
    except KeyboardInterrupt:
        print()
        log_info("Shutting down...")


if __name__ == '__main__':
    main()
