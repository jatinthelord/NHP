#!/usr/bin/perl
# NHP Language Server - server_nhp.pl
# Perl se NHP compiler ko server-side banata hai
# Install: perl server_nhp.pl
# Port: 8080

use strict;
use warnings;
use IO::Socket::INET;
use POSIX qw(strftime);
use File::Basename;
use Cwd 'abs_path';

my $PORT      = 8080;
my $COMPILER  = "./nhpc";   # nhp_compiler.c se banaya hua binary
my $WORKDIR   = "/tmp/nhp_server_work";
my $DBFILE    = "/tmp/nhp_server_db.txt";
my $VERSION   = "1.0";

mkdir $WORKDIR unless -d $WORKDIR;

print "NHP Language Server v$VERSION\n";
print "Port: $PORT\n";
print "Compiler: $COMPILER\n";
print "Work dir: $WORKDIR\n\n";

# ── flat-file database ──
sub db_save {
    my ($key, $val) = @_;
    my %db = db_load_all();
    $db{$key} = $val;
    open my $fh, '>', $DBFILE or return;
    for my $k (sort keys %db) {
        my $v = $db{$k};
        $v =~ s/\n/\\n/g;
        print $fh "$k=$v\n";
    }
    close $fh;
}
sub db_get {
    my ($key) = @_;
    my %db = db_load_all();
    return $db{$key} // '';
}
sub db_load_all {
    my %db;
    return %db unless -f $DBFILE;
    open my $fh, '<', $DBFILE or return %db;
    while (<$fh>) {
        chomp;
        next unless /^(.+?)=(.*)$/;
        my ($k,$v) = ($1,$2);
        $v =~ s/\\n/\n/g;
        $db{$k} = $v;
    }
    close $fh;
    return %db;
}

# ── compile NHP source ──
sub compile_nhp {
    my ($src) = @_;
    my $ts   = time();
    my $srcf = "$WORKDIR/nhp_$ts.nhp";
    my $asmf = "$WORKDIR/nhp_$ts.asm";

    open my $fh, '>', $srcf or return (0, "Cannot write source file");
    print $fh $src;
    close $fh;

    unless (-x $COMPILER) {
        # try to build compiler if nhp_compiler.c exists
        if (-f "nhp_compiler.c") {
            system("gcc -O2 -o nhpc nhp_compiler.c 2>/dev/null");
        }
        unless (-x $COMPILER) {
            return (0, "NHP compiler not found. Run: gcc -O2 -o nhpc nhp_compiler.c");
        }
    }

    my $out = `$COMPILER $srcf -o $asmf 2>&1`;
    my $ok  = -f $asmf;
    my $asm = '';
    if ($ok) {
        open my $af, '<', $asmf or return (0, "Cannot read asm output");
        local $/; $asm = <$af>; close $af;
    }
    unlink $srcf, $asmf;
    return ($ok, $ok ? $asm : $out);
}

# ── URL decode ──
sub urldecode {
    my ($s) = @_;
    $s =~ s/%([0-9A-Fa-f]{2})/chr(hex($1))/ge;
    $s =~ s/\+/ /g;
    return $s;
}

# ── parse POST body ──
sub parse_post {
    my ($body) = @_;
    my %p;
    for my $pair (split /&/, $body) {
        my ($k, $v) = split /=/, $pair, 2;
        $p{urldecode($k)} = urldecode($v // '');
    }
    return %p;
}

# ── HTTP response helper ──
sub http_response {
    my ($conn, $code, $type, $body) = @_;
    my $status = $code == 200 ? "200 OK"
               : $code == 404 ? "404 Not Found"
               : $code == 400 ? "400 Bad Request"
               : "500 Internal Server Error";
    my $len = length($body);
    my $date = strftime("%a, %d %b %Y %H:%M:%S GMT", gmtime);
    print $conn "HTTP/1.1 $status\r\n";
    print $conn "Date: $date\r\n";
    print $conn "Server: NHPServer/$VERSION\r\n";
    print $conn "Content-Type: $type; charset=utf-8\r\n";
    print $conn "Content-Length: $len\r\n";
    print $conn "Access-Control-Allow-Origin: *\r\n";
    print $conn "Connection: close\r\n\r\n";
    print $conn $body;
}

# ── main HTML page ──
sub page_home {
    return <<'HTML';
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>NHP Language Server</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;}
body{font-family:monospace;background:#0f0f0f;color:#c8c8c8;font-size:14px;}
#header{background:#1a1a2e;border-bottom:1px solid #2a2a4a;padding:10px 20px;display:flex;align-items:center;gap:16px;}
#header h1{color:#00ccff;font-size:18px;letter-spacing:2px;}
#header span{color:#666;font-size:11px;}
#wrap{display:grid;grid-template-columns:1fr 1fr;height:calc(100vh - 45px);}
.pane{display:flex;flex-direction:column;border-right:1px solid #1a1a1a;}
.pane-title{background:#151515;padding:6px 14px;font-size:11px;color:#666;letter-spacing:2px;border-bottom:1px solid #1a1a1a;}
textarea{flex:1;background:#0a0a0a;color:#00ff88;font-family:'Courier New',monospace;font-size:13px;padding:14px;border:none;outline:none;resize:none;}
#output{flex:1;font-family:'Courier New',monospace;font-size:12px;padding:14px;overflow-y:auto;white-space:pre;color:#aaffaa;background:#050505;}
.toolbar{background:#151515;border-top:1px solid #1a1a1a;padding:8px 14px;display:flex;gap:8px;align-items:center;}
button{padding:5px 16px;font-family:monospace;font-size:12px;cursor:pointer;border:1px solid #333;background:#1a1a1a;color:#ccc;letter-spacing:1px;}
button:hover{background:#2a2a3a;color:#fff;}
button.primary{background:#1a2a6c;color:#fff;border-color:#3a4a9c;}
button.primary:hover{background:#2a3a8c;}
#status{margin-left:auto;font-size:11px;color:#666;}
#status.ok{color:#00ff88;}
#status.err{color:#ff4444;}
</style>
</head>
<body>
<div id="header">
  <h1>NHP</h1>
  <span>Language Server v1.0 | NHP -> x86-64 NASM | Server-Side Compiler</span>
</div>
<div id="wrap">
  <div class="pane">
    <div class="pane-title">NHP SOURCE — write your code</div>
    <textarea id="src" spellcheck="false">; NHP Hello World
Pset "Hello from server!\n" _len -o
msg = [20]
  .bytes 72 101 108 108 111 32 115 101 114 118 101 114 33 10
_begin:

Maths * int
  IMP SCR, 12
  IMP SCT, 81
  Add SCR, SCT
  cls SCR, 0
  jns done
done:
  Print msg</textarea>
    <div class="toolbar">
      <button class="primary" onclick="compileCode()">Compile NHP</button>
      <button onclick="clearCode()">Clear</button>
      <button onclick="saveCode()">Save to DB</button>
      <button onclick="loadCode()">Load from DB</button>
      <span id="status">ready</span>
    </div>
  </div>
  <div class="pane">
    <div class="pane-title">NASM x86-64 OUTPUT</div>
    <div id="output">; Click "Compile NHP" to see output</div>
    <div class="toolbar">
      <button onclick="copyAsm()">Copy ASM</button>
      <button onclick="downloadAsm()">Download .asm</button>
      <span id="out-size" style="margin-left:auto;font-size:11px;color:#666;">0 bytes</span>
    </div>
  </div>
</div>
<script>
var lastAsm='';
async function compileCode(){
  var src=document.getElementById('src').value;
  if(!src.trim()){setStatus('empty','err');return;}
  setStatus('compiling...','');
  try{
    var r=await fetch('/compile',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'src='+encodeURIComponent(src)});
    var j=await r.json();
    lastAsm=j.asm||j.error||'';
    document.getElementById('output').textContent=lastAsm;
    document.getElementById('out-size').textContent=lastAsm.length+' bytes';
    setStatus(j.ok?'OK':'ERROR',j.ok?'ok':'err');
  }catch(e){setStatus('server error','err');document.getElementById('output').textContent='Error: '+e.message;}
}
async function saveCode(){
  var src=document.getElementById('src').value;
  await fetch('/db',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'action=save&key=last_code&val='+encodeURIComponent(src)});
  setStatus('saved','ok');
}
async function loadCode(){
  var r=await fetch('/db?action=get&key=last_code');
  var j=await r.json();
  if(j.val)document.getElementById('src').value=j.val;
  setStatus('loaded','ok');
}
function setStatus(m,c){var e=document.getElementById('status');e.textContent=m;e.className=c;}
function clearCode(){document.getElementById('src').value='';document.getElementById('output').textContent='';setStatus('ready','');}
function copyAsm(){if(lastAsm)navigator.clipboard.writeText(lastAsm);}
function downloadAsm(){if(!lastAsm)return;var b=new Blob([lastAsm],{type:'text/plain'});var a=document.createElement('a');a.href=URL.createObjectURL(b);a.download='nhp_out.asm';a.click();}
</script>
</body>
</html>
HTML
}

# ── request handler ──
sub handle_request {
    my ($conn) = @_;
    my $request = '';
    while (my $line = <$conn>) {
        $request .= $line;
        last if $line eq "\r\n";
    }
    return unless $request;

    my ($method, $path) = $request =~ /^(GET|POST)\s+(\S+)/i;
    $method //= 'GET'; $path //= '/';

    # read POST body
    my $body = '';
    if ($method eq 'POST' && $request =~ /Content-Length:\s*(\d+)/i) {
        my $len = $1;
        read($conn, $body, $len) if $len > 0;
    }

    my $time = strftime("%H:%M:%S", localtime);
    print "[$time] $method $path\n";

    # ── routes ──
    if ($path eq '/' || $path eq '/index.html') {
        http_response($conn, 200, 'text/html', page_home());
    }
    elsif ($path eq '/compile' && $method eq 'POST') {
        my %p = parse_post($body);
        my $src = $p{src} // '';
        if (!$src) {
            http_response($conn, 400, 'application/json', '{"ok":false,"error":"No source"}');
            return;
        }
        my ($ok, $result) = compile_nhp($src);
        # escape JSON
        $result =~ s/\\/\\\\/g; $result =~ s/"/\\"/g; $result =~ s/\n/\\n/g; $result =~ s/\r//g;
        my $key = $ok ? 'asm' : 'error';
        http_response($conn, 200, 'application/json', "{\"ok\":".($ok?'true':'false').",\"$key\":\"$result\"}");
    }
    elsif ($path =~ m{^/db} ) {
        if ($method eq 'POST') {
            my %p = parse_post($body);
            db_save($p{key}//'', $p{val}//'');
            http_response($conn, 200, 'application/json', '{"ok":true}');
        } else {
            # GET
            $path =~ /key=([^&]+)/;
            my $key = urldecode($1 // '');
            my $val = db_get($key);
            $val =~ s/\\/\\\\/g; $val =~ s/"/\\"/g; $val =~ s/\n/\\n/g;
            http_response($conn, 200, 'application/json', "{\"ok\":true,\"val\":\"$val\"}");
        }
    }
    elsif ($path eq '/status') {
        my $uptime = time();
        http_response($conn, 200, 'application/json',
            "{\"server\":\"NHPServer\",\"version\":\"$VERSION\",\"port\":$PORT}");
    }
    else {
        http_response($conn, 404, 'text/plain', '404 Not Found');
    }
}

# ── main server loop ──
my $server = IO::Socket::INET->new(
    LocalPort => $PORT,
    Type      => SOCK_STREAM,
    Reuse     => 1,
    Listen    => 10,
) or die "Cannot bind port $PORT: $!\nTry: perl server_nhp.pl\n";

print "Server running at http://localhost:$PORT\n";
print "Open this URL in your browser\n\n";

while (my $conn = $server->accept()) {
    $conn->autoflush(1);
    eval { handle_request($conn) };
    close $conn;
}
