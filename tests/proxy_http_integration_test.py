"""Loopback fixtures exercise the real HttpClient and libcurl, no GFN account."""
import socket, threading, subprocess, os, base64
from pathlib import Path
BINARY = Path('build/host-tests/proxy_http_client_probe' + ('.exe' if os.name == 'nt' else '')).resolve()
Path('build/host-tests/proxy-log-fixture').mkdir(parents=True, exist_ok=True)

def exact(sock, count):
    result = b''
    while len(result) < count:
        chunk = sock.recv(count-len(result))
        if not chunk: raise AssertionError('unexpected EOF')
        result += chunk
    return result

def headers(sock):
    data = b''
    while b'\r\n\r\n' not in data:
        data += exact(sock, 1)
        assert len(data) < 8192
    return data

def test(scheme, rejected=False):
    server = socket.socket(); server.bind(('127.0.0.1', 0)); server.listen(1)
    port = server.getsockname()[1]; errors = []
    def handle():
        try:
            server.settimeout(10)
            with server.accept()[0] as client:
                client.settimeout(5)
                if scheme != 'http':
                    version, count = exact(client,2)
                    assert version == 5 and 2 in exact(client,count)
                    client.sendall(b'\x05\x02')
                    version, count = exact(client,2)
                    assert version == 1 and exact(client,count) == b'u@'
                    assert exact(client,exact(client,1)[0]) == b'p:#%'
                    client.sendall(b'\x01\x00')
                    version, command, reserved, kind = exact(client,4)
                    assert (version,command,reserved) == (5,1,0)
                    if scheme == 'socks5h':
                        assert kind == 3 and exact(client,exact(client,1)[0]) == b'fixture.invalid'
                    else:
                        assert kind == 1 and exact(client,4) == socket.inet_aton('127.0.0.2')
                    assert exact(client,2) == b'\x00\x50'
                    client.sendall(b'\x05\x00\x00\x01\x7f\x00\x00\x01\x00\x50')
                request = headers(client)
                if scheme == 'http':
                    token = base64.b64encode(b'u@:p:#%')
                    assert b'Proxy-Authorization: Basic ' + token in request
                    assert request.startswith(b'CONNECT ' if rejected else b'GET http://fixture.invalid/probe ')
                else: assert request.startswith(b'GET /probe ')
                client.sendall(b'HTTP/1.1 407 Proxy Authentication Required\r\nProxy-Authenticate: Basic realm="fixture"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n' if rejected else b'HTTP/1.1 200 OK\r\nContent-Length: 8\r\nConnection: close\r\n\r\nproxy-ok')
        except Exception as error: errors.append(error)
        finally: server.close()
    thread = threading.Thread(target=handle); thread.start()
    env=os.environ.copy()
    if os.name == 'nt': env['PATH']='C:/msys64/mingw64/bin;'+env['PATH']
    # A deliberately conflicting no_proxy verifies explicit proxy routing wins.
    env['NO_PROXY']='*'; env['no_proxy']='*'
    target='https://fixture.invalid/probe' if rejected else ('http://127.0.0.2/probe' if scheme=='socks5' else 'http://fixture.invalid/probe')
    result=subprocess.run([str(BINARY),scheme,str(port),target]+(['reject'] if rejected else []),env=env,capture_output=True,text=True,timeout=15)
    thread.join(11)
    assert not thread.is_alive()
    assert not errors, errors
    assert result.returncode==0, result.stderr
    print(result.stdout.strip())

for scheme in ['http','socks5','socks5h']: test(scheme)
test('http',True)
log=Path('build/host-tests/proxy-log-fixture/proxy.log').read_text(encoding='utf8')
assert 'connect_status=407' in log
assert 'p:#%' not in log and 'u@' not in log
print('Production proxy HTTP/SOCKS5/SOCKS5h auth, DNS mode, NO_PROXY override and CONNECT 407 diagnostics passed.')
