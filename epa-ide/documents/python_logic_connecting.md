# Connecting External Python Logic

## Session File

When the C++ host starts and the ext-logic bridge is enabled it writes a session descriptor to a temporary file:

    /tmp/elara-ext-logic-<backend-id>.json

Contents:

    {
      "host": "127.0.0.1",
      "port": 19100,
      "backend_id": "org.elara.ui.my-app"
    }

Python scripts read this file to find the host address before connecting.

## Attaching

    import socket, json

    with open("/tmp/elara-ext-logic-org.elara.ui.my-app.json") as f:
        session = json.load(f)

    sock = socket.socket()
    sock.connect((session["host"], session["port"]))

    # Register
    sock.sendall(b'{"method":"register","params":{"name":"my-script"}}\n')
    response = sock.recv(4096)

## Sending Commands

Commands follow the same JSON-RPC shape as the AI RPC:

    def call(sock, method, params):
        msg = json.dumps({"method": method, "params": params}) + "\n"
        sock.sendall(msg.encode())
        return json.loads(sock.recv(65536))

    call(sock, "setText", {"target": "app.status", "value": "Python connected"})

## Receiving Events

The bridge forwards forwarded events as unsolicited JSON lines. Read them in a background thread:

    def listen(sock):
        buf = b""
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                event = json.loads(line)
                handle_event(event)

## Clean Disconnect

Always close the connection when done. Leaving old clients connected causes a chain of reconnect attempts and interferes with subsequent sessions:

    sock.close()

## IDE Ext-Logic Bridge

When the project is open in the IDE the ext-logic client is the IDE's `ext_logic_client.py`. Python scripts that run via Script Buttons share the same bridge session. Only one external Python script should be connected at a time.
