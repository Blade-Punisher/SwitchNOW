import http.server,threading,time,subprocess,os
from pathlib import Path
class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def do_GET(self):
        if self.path=='/slow': time.sleep(3)
        body=b'image-body'
        try:
            self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body);self.wfile.flush()
        except (BrokenPipeError,ConnectionResetError,OSError): pass
    def log_message(self,*args): pass
class Server(http.server.ThreadingHTTPServer):
    daemon_threads=True
server=Server(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever);thread.start()
try:
    path=Path('build/host-tests/image_http_session_probe'+('.exe' if os.name=='nt' else '')).resolve()
    env=os.environ.copy()
    if os.name=='nt': env['PATH']='C:/msys64/mingw64/bin;'+env['PATH']
    env['http_proxy']='http://127.0.0.1:1';env['NO_PROXY']='';env['no_proxy']=''
    result=subprocess.run([str(path),'http://127.0.0.1:'+str(server.server_port)],capture_output=True,text=True,env=env,timeout=15)
    assert result.returncode==0,result.stdout+result.stderr
    print(result.stdout.strip())
finally:
    server.shutdown();server.server_close();thread.join()
