"""
Pre-build patch: Replace dynamic char[] allocations in src/main.cpp
with static buffers to avoid heap fragmentation under sustained traffic.

This script is called by platformio during the pre stage.
It modifies src/main.cpp inline to use static buffers for N2K->SeaSmart
and NMEA0183 message formatting, eliminating repeated heap allocation
in the hot path.
"""

Import("env")
import os
import re
import sys

def patch_main_allocations(project_dir):
    main_cpp = os.path.join(project_dir, "src", "main.cpp")
    gwbuffer_cpp = os.path.join(project_dir, "lib", "queue", "GwBuffer.cpp")
    gwsocketconnection_cpp = os.path.join(project_dir, "lib", "socketserver", "GwSocketConnection.cpp")
    gwsocketserver_cpp = os.path.join(project_dir, "lib", "socketserver", "GwSocketServer.cpp")
    gwtcpclient_cpp = os.path.join(project_dir, "lib", "socketserver", "GwTcpClient.cpp")
    
    if not os.path.exists(main_cpp):
        print("[patch_main_allocs] src/main.cpp not found, skipping patch")
        return
    
    with open(main_cpp, 'r') as f:
        content = f.read()
    
    original_content = content
    
    # Patch 1: handleN2kMessage - replace dynamic buf allocation
    # Before: char *buf=new char[MAX_NMEA2000_MESSAGE_SEASMART_SIZE+3];
    # After: static char buf[MAX_NMEA2000_MESSAGE_SEASMART_SIZE+3];
    pattern1 = r'void handleN2kMessage\(const tN2kMsg &n2kMsg,int sourceId, bool isConverted=false\)\s*\{[^}]*?char \*buf=new char\[MAX_NMEA2000_MESSAGE_SEASMART_SIZE\+3\];'
    
    # More surgical: just replace the line itself
    content = re.sub(
        r'(\n\s+)char \*buf=new char\[MAX_NMEA2000_MESSAGE_SEASMART_SIZE\+3\];',
        r'\1char buf[MAX_NMEA2000_MESSAGE_SEASMART_SIZE+3];',
        content,
        count=1
    )
    
    # Remove the deletion line if it exists
    content = re.sub(
        r'(\n\s+)std::unique_ptr<char> bufDel\(buf\);',
        r'',
        content,
        count=1
    )
    
    # Patch 2: SendNMEA0183Message - replace dynamic buf allocation
    # Before: char *buf=new char[MAX_NMEA0183_MESSAGE_SIZE+3];
    # After: char buf[MAX_NMEA0183_MESSAGE_SIZE+3];
    content = re.sub(
        r'(\n\s+)char \*buf=new char\[MAX_NMEA0183_MESSAGE_SIZE\+3\];',
        r'\1char buf[MAX_NMEA0183_MESSAGE_SIZE+3];',
        content,
        count=1
    )
    
    # Remove all remaining unique_ptr<char> bufDel guards (idempotent global sweep).
    # IMPORTANT: must replace ALL occurrences because any guard removed without also
    # converting its paired new[] to a stack buffer becomes a permanent leak.
    content = re.sub(
        r'\n\s+std::unique_ptr<char> bufDel\(buf\);',
        r'',
        content
    )

    # Patch 2b: XdrMappedRequest::processRequest - heap buf with guard removed above,
    # convert to stack buffer so it is properly freed on scope exit.
    content = re.sub(
        r'(\n\s+)char \*buf=new char\[MAX_NMEA0183_MSG_BUF_LEN\+2\];',
        r'\1char buf[MAX_NMEA0183_MSG_BUF_LEN+2];',
        content,
        count=1
    )

    # Write back only if changed
    if content != original_content:
        with open(main_cpp, 'w') as f:
            f.write(content)
        print("[patch_main_allocs] Patched handleN2kMessage and SendNMEA0183Message to use static buffers")
    else:
        print("[patch_main_allocs] No changes needed")

    # Patch 3: GwBuffer destructor must use delete[] for array allocation
    if not os.path.exists(gwbuffer_cpp):
        print("[patch_main_allocs] lib/queue/GwBuffer.cpp not found, skipping GwBuffer patch")
        return

    with open(gwbuffer_cpp, 'r') as f:
        gwbuffer_content = f.read()

    gwbuffer_original = gwbuffer_content
    gwbuffer_content = re.sub(
        r'(\n\s*)delete buffer;',
        r'\1delete[] buffer;',
        gwbuffer_content,
        count=1
    )

    if gwbuffer_content != gwbuffer_original:
        with open(gwbuffer_cpp, 'w') as f:
            f.write(gwbuffer_content)
        print("[patch_main_allocs] Patched GwBuffer destructor to use delete[]")

    # Patch 4: Tune TCP backpressure handling
    if not os.path.exists(gwsocketconnection_cpp):
        print("[patch_main_allocs] lib/socketserver/GwSocketConnection.cpp not found, skipping slow-client patch")
        return

    with open(gwsocketconnection_cpp, 'r') as f:
        gwsocket_content = f.read()

    gwsocket_original = gwsocket_content

    # Cleanup from older patch variants that introduced header dependency.
    gwsocket_content = re.sub(r'^\s*lastOverflowTime\s*=\s*0;\s*\n', '', gwsocket_content, flags=re.MULTILINE)
    gwsocket_content = re.sub(r'^\s*if\s*\(lastOverflowTime\s*==\s*0\)\s*\n\s*lastOverflowTime\s*=\s*millis\(\);\s*\n', '', gwsocket_content, flags=re.MULTILINE)
    gwsocket_content = re.sub(r'^\s*unsigned\s+long\s+elapsedMs\s*=\s*millis\(\)\s*-\s*lastOverflowTime;\s*\n', '', gwsocket_content, flags=re.MULTILINE)
    gwsocket_content = gwsocket_content.replace("        if (overflows >= 5 || elapsedMs >= 500)\n", "        if (overflows >= 120)\n")
    gwsocket_content = gwsocket_content.replace("            LOG_DEBUG(GwLog::ERROR, \"disconnecting slow TCP client %s (%d overflows, %ldms backpressure)\", \n                remoteIpAddress.c_str(), overflows, elapsedMs);\n",
                                              "            LOG_DEBUG(GwLog::ERROR, \"disconnecting stalled TCP client %s after %d consecutive overflows\",\n                remoteIpAddress.c_str(), overflows);\n")
    gwsocket_content = gwsocket_content.replace("        lastOverflowTime = 0;\n", "")

    new_enqueue = '''bool GwSocketConnection::enqueue(uint8_t *data, size_t len)
{
    if (len == 0)
        return true;
    size_t rt = buffer->addData(data, len);
    if (rt < len)
    {
        overflows++;
        // Buffer is fixed-size, so this is backpressure, not growth.
        // Drop overflowing messages and disconnect only if the client is truly stalled.
        if (overflows >= 120)
        {
            LOG_DEBUG(GwLog::ERROR, "disconnecting stalled TCP client %s after %d consecutive overflows",
                remoteIpAddress.c_str(), overflows);
            stop();
        }
        else if (overflows == 1)
        {
            LOG_DEBUG(GwLog::LOG, "TCP backpressure on %s (dropping current message)", remoteIpAddress.c_str());
        }
        else if ((overflows % 30) == 0)
        {
            LOG_DEBUG(GwLog::LOG, "TCP backpressure persists on %s (%d consecutive drops)",
                remoteIpAddress.c_str(), overflows);
        }
        return false;
    }
    if (overflows > 0)
    {
        overflows = 0;
    }
    return true;
}
'''

    new_connected = '''bool GwSocketConnection::connected()
{
    // Keep this check allocation/syscall free.
    // Link health is tracked by read/write error handling and TCP keepalive.
    return fd >= 0;
}
'''

    new_write = '''GwBuffer::WriteStatus GwSocketConnection::write()
{
    if (!hasClient())
    {
        LOG_DEBUG(GwLog::LOG, "write called on empty client");
        return GwBuffer::ERROR;
    }
    if (!buffer->usedSpace())
    {
        pendingWrite = false;
        writeError = false;
        return GwBuffer::OK;
    }
    buffer->fetchData(
        -1, [](uint8_t *buffer, size_t len, void *param) -> size_t
        {
            GwSocketConnection *c = (GwSocketConnection *)param;

            // If send queue is already backed up, avoid tight-loop retries.
            if (c->pendingWrite && (millis() - c->lastWrite) < 5)
            {
                return 0;
            }

            int res = send(c->fd, (void *)buffer, len, MSG_DONTWAIT);
            if (res > 0)
            {
                if ((size_t)res >= len)
                {
                    c->pendingWrite = false;
                    c->writeError = false;
                }
                else
                {
                    if (!c->pendingWrite)
                    {
                        c->lastWrite = millis();
                        c->pendingWrite = true;
                    }
                    else if (millis() >= (c->lastWrite + c->writeTimeout))
                    {
                        c->logger->logDebug(GwLog::ERROR, "Write timeout on channel %s", c->remoteIpAddress.c_str());
                        c->writeError = true;
                    }
                }
                return (size_t)res;
            }

            if (res < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                if (!c->pendingWrite)
                {
                    c->lastWrite = millis();
                    c->pendingWrite = true;
                }
                else if (millis() >= (c->lastWrite + c->writeTimeout))
                {
                    c->logger->logDebug(GwLog::ERROR, "Write timeout on channel %s", c->remoteIpAddress.c_str());
                    c->writeError = true;
                }
                return 0;
            }

            c->logger->logDebug(GwLog::LOG, "client write error %d on %s", errno, c->remoteIpAddress.c_str());
            c->writeError = true;
            c->stop();
            return 0;
        },
        this);
    if (writeError)
    {
        LOG_DEBUG(GwLog::DEBUG + 1, "write error on %s", remoteIpAddress.c_str());
        return GwBuffer::ERROR;
    }

    return GwBuffer::OK;
}
'''

    enqueue_pattern = r'bool GwSocketConnection::enqueue\(uint8_t \*data, size_t len\)\n\{[\s\S]*?\n\}'
    gwsocket_content, enqueue_repl_count = re.subn(enqueue_pattern, new_enqueue.rstrip(), gwsocket_content, count=1)
    if enqueue_repl_count == 0 and "disconnecting slow TCP client" not in gwsocket_content:
        print("[patch_main_allocs] WARNING: enqueue() pattern not found in GwSocketConnection.cpp")

    connected_pattern = r'bool GwSocketConnection::connected\(\)\n\{[\s\S]*?\n\}'
    gwsocket_content, connected_repl_count = re.subn(connected_pattern, new_connected.rstrip(), gwsocket_content, count=1)
    if connected_repl_count == 0:
        print("[patch_main_allocs] WARNING: connected() pattern not found in GwSocketConnection.cpp")

    write_pattern = r'GwBuffer::WriteStatus GwSocketConnection::write\(\)\n\{[\s\S]*?\n\}'
    gwsocket_content, write_repl_count = re.subn(write_pattern, new_write.rstrip(), gwsocket_content, count=1)
    if write_repl_count == 0:
        print("[patch_main_allocs] WARNING: write() pattern not found in GwSocketConnection.cpp")

    gwsocket_content = gwsocket_content.replace("        if (errno != EAGAIN)",
                                                "        if (errno != EAGAIN && errno != EWOULDBLOCK)")

    if gwsocket_content != gwsocket_original:
        with open(gwsocketconnection_cpp, 'w') as f:
            f.write(gwsocket_content)
        print("[patch_main_allocs] Patched GwSocketConnection TCP handling (enqueue/connected/error)")

    # Patch 4b: avoid per-message connected() probing in fanout path
    if os.path.exists(gwsocketserver_cpp):
        with open(gwsocketserver_cpp, 'r') as f:
            gwserver_content = f.read()
        gwserver_original = gwserver_content
        gwserver_content = gwserver_content.replace(
            "        if (client->connected())\n        {\n            if(client->enqueue((uint8_t *)buf, len)) hasSend=true;\n        }\n",
            "        if (client->enqueue((uint8_t *)buf, len)) hasSend=true;\n"
        )
        if gwserver_content != gwserver_original:
            with open(gwsocketserver_cpp, 'w') as f:
                f.write(gwserver_content)
            print("[patch_main_allocs] Patched GwSocketServer fanout to remove per-message connected() checks")

    # Patch 5: Disable TCP_NODELAY for TCP stream paths (allows packet coalescing and reduces lwIP pressure)
    for path in [gwsocketserver_cpp, gwtcpclient_cpp]:
        if not os.path.exists(path):
            continue
        with open(path, 'r') as f:
            tcp_content = f.read()
        tcp_original = tcp_content
        tcp_content = tcp_content.replace("GwSocketHelper::setKeepAlive(client_sock,true)",
                                          "GwSocketHelper::setKeepAlive(client_sock,false)")
        tcp_content = tcp_content.replace("GwSocketHelper::setKeepAlive(sockfd,true)",
                                          "GwSocketHelper::setKeepAlive(sockfd,false)")
        if tcp_content != tcp_original:
            with open(path, 'w') as f:
                f.write(tcp_content)
            print(f"[patch_main_allocs] Patched TCP_NODELAY policy in {os.path.basename(path)}")

# Run immediately when this pre-script is loaded.
project_root = env.get('PROJECT_DIR', os.getcwd())
print(f"[patch_main_allocs] Running patch for project root: {project_root}")
patch_main_allocations(project_root)

if __name__ == "__main__":
    project_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    patch_main_allocations(project_dir)
