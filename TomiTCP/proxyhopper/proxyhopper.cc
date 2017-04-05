#include <iostream>
#include <string>
#include <memory>
#include <cstdio>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include "../net.h"
#include "../str.h"
using namespace std;
using namespace net;

const int proxy_timeout = 60;

int sendall(int s, const char* buf, const int size);
void goon(int a, int b);

TomiTCP out, srv;
auto_ptr<TomiTCP> in;

bool remov = true;
char *lasthost = 0;

/*
 * Hop to next host using HTTP CONNECT
 */
void httpconnect(TomiTCP &c, char *host, bool last)
{
    if (!host || !*host)
	return;

    char *mylasthost = lasthost;
    lasthost = host;

    cerr << "HTTP Connect Proxy connecting to " << host << endl;
    fprintf(c, "CONNECT %s HTTP/1.0\n\n", host);

    if (input_timeout(c.sock, proxy_timeout * 1000) <= 0) {
	/*
	 * The proxy wasn't able to connect to the host we wanted
	 * within a specified time.
	 */
	if (!last && remov) {
	    cerr << "Removing " << host << endl;
	    *host = 0;
	}
	throw runtime_error("Connection timeout");
    }

    string line;
    if (c.getline(line)) {
	chomp(line);
	char httpver[10], err[100] = ""; int code;
	int ret = sscanf(line.c_str(), "HTTP/%9[^ ] %i %99[^\n]", httpver, &code, err);
	if (ret < 2) {
	    /*
	     * The proxy replied with some unknown garbage.
	     */
	    if (mylasthost && remov) {
		cerr << "Removing " << mylasthost << endl;
		*mylasthost = 0;
	    }
	    throw runtime_error("Parse error on HTTP response");
	}
	if (code < 200 || code > 299) {
	    /*
	     * The proxy wasn't able to connect to host we wanted.
	     */
	    if (code >= 500) {
		if (!last && remov) {
		    cerr << "Removing " << host << endl;
		    *host = 0;
		}
	    }
	    /*
	     * The proxy didn't want to connect.
	     */
	    else if (code >= 400 && mylasthost && remov) {
		cerr << "Removing " << mylasthost << endl;
		*mylasthost = 0;
	    }

	    throw runtime_error("HTTP not ok! (" + tostr<int>(code) + " " +
		    string(err) + ")");
	}
    } else {
	/*
	 * The proxy didn't want to talk.
	 */
	if (mylasthost && remov) {
	    cerr << "Removing " << mylasthost << endl;
	    *mylasthost = 0;
	}
	throw runtime_error("Zero sized HTTP reply");
    }

    /*
     * Skip HTTP headers.
     */
    while (c.getline(line)) {
	chomp(line);
	if (line.empty())
	    break;
    }
}

/*
 * Hop to next host using SOCKS v4
 */
void socks(TomiTCP &c, char *host, bool last)
{
    if (!host || !*host)
	return;

    char *mylasthost = lasthost;
    lasthost = host;

    vector<sockaddr_uni> addrs;

    /*
     * Parse it
     */
    string addr, port, h(host);
    unsigned int colon = h.rfind(':');
    if (colon == string::npos)
	throw runtime_error("Invalid host (" + h + ")");
    addr.assign(h, 0, colon);
    port.assign(h, colon + 1, string::npos);

    resolve(addr, port, addrs);
    while (addrs.size() && addrs[0].sa.sa_family != AF_INET)
	addrs.erase(addrs.begin());
    if (!addrs.size())
	throw runtime_error("Could not resolve address");

    char sbuf[9] = "\x04\x01      ";
    memcpy(sbuf + 2, &PORT_SOCKADDR(addrs[0]), 2);
    memcpy(sbuf + 4, &addrs[0].sin.sin_addr.s_addr, 4);

    cerr << "SOCKS v4 Proxy connecting to " << host << endl;
    fwrite(sbuf, 9, 1, c);

    if (input_timeout(c.sock, proxy_timeout * 1000) <= 0) {
	/*
	 * The proxy wasn't able to connect to the host we wanted
	 * within a specified time.
	 */
	if (!last && remov) {
	    cerr << "Removing " << host << endl;
	    *host = 0;
	}
	throw runtime_error("Connection timeout");
    }

    char buf[8];
    if (fread(buf, 8, 1, c) != 1) {
	/*
	 * The proxy didn't want to talk.
	 */
	if (mylasthost && remov) {
	    cerr << "Removing " << mylasthost << endl;
	    *mylasthost = 0;
	}
	throw runtime_error("Bad or no reply");
    }

    if (buf[0] != 0 || buf[1] != 90) {
	/*
	 * The request was not successful
	 */
	throw runtime_error("Bad or unsuccessful reply");
    }
}

int main(int argc, char *argv[])
{
    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    if (argc < 3) {
	cerr << "Usage: proxyhopper [-p] <port> [proxy:port] [-sh] [proxy:port...] [-sh] <host>:<port>"
	    << endl << " port -1 - single mode" << endl
	    << " -p - permanent mode, don't remove proxys on error" << endl
	    << " -s - SOCKS v4, -h - HTTP CONNECT" << endl
	    << " (yes, you specify the proxy type AFTER the address and it" << endl
	    << "  takes effect on all following proxys)" << endl;
	return -1;
    }

    bool single = false;

    int arg = 1;

    char *a1 = argv[arg++];
    if (a1[0] == '-') {
	for (char *c = a1 + 1; *c; c++)
	    switch (*c) {
		case 'p':
		    remov = false;
		    break;
		case '1':
		    single = true;
		    break;
		default:
		    cerr << "Unknown option" << endl;
		    return -1;
	    }

	if (!single)
	    a1 = argv[arg++];
    }

    int port = atol(a1);

    if (!single) {
	try {
	    srv.listen(port);
	} catch (runtime_error e) {
	    cerr << e.what() << endl;
	    return -1;
	}
    }

    while (1) {
	int marg = arg;
	try {
	    if (!single)
		in.reset(srv.accept());

	    /*
	     * Skip removed hosts
	     */
	    while (marg < argc && (!argv[marg][0] || argv[marg][0] == '-'))
		marg++;
	    if (marg >= argc) {
		cerr << "There are no hosts left!" << endl;
		return -1;
	    }

	    /*
	     * Parse the first host
	     */
	    lasthost = argv[marg];
	    string firsthost, firstport, h(argv[marg++]);
	    unsigned int colon = h.rfind(':');
	    if (colon == string::npos) {
		cerr << "Invalid host (" << h << ")" << endl;
		return -1;
	    }
	    firsthost.assign(h, 0, colon);
	    firstport.assign(h, colon + 1, string::npos);

	    /*
	     * Connect the first host
	     */
	    cerr << "Connecting to " << firsthost << ":" << firstport << endl;
	    try {
		out.connect(firsthost, firstport);
	    } catch (...) {
		if (remov)
		    *lasthost = 0;
		throw;
	    }

	    /*
	     * Proxy mode
	     *  1 - HTTP CONNECT
	     *  2 - SOCKS v4
	     */
	    int mode = 1;

	    /*
	     * Hop over proxys
	     */
	    while (marg < argc) {
		if (argv[marg][0] == '-') {
		    switch (argv[marg++][1]) {
			case 'h':
			    mode = 1;
			    break;
			case 's':
			    mode = 2;
			    break;
			default:
			    cerr << "Unknown parameter" << endl;
			    return -1;
		    }
		} else {
		    switch (mode) {
			case 1:
			    httpconnect(out, argv[marg++], (marg + 1) == argc); break;
			case 2:
			    socks(out, argv[marg++], (marg + 1) == argc); break;
		    }
		}
	    }

	    cerr << "Ready, Go on!" << endl;

	    if (single) {
		goon(0, out.sock);
		cerr << "Connection closed" << endl << endl;
	    } else {
		/*
		 * We can handle the connection in another process, as we don't
		 * need to modify anything here.
		 */
		int ret = fork();
		if (ret < 0)
		    throw runtime_error(strerror(errno));
		if (!ret) {
		    try {
			srv.close();

			/*
			 * Pass data between sockets
			 */
			goon(in->sock, out.sock);
			cerr << "Connection closed" << endl << endl;
			return 0;
		    } catch (runtime_error e) {
			cerr << e.what() << endl << endl;
			return -1;
		    }
		}
	    }
	} catch (runtime_error e) {
	    cerr << e.what() << endl << endl;
	}
	out.close();
	in.reset(0);

	if (single)
	    break;
    }

    return 0;
}

int sendall(int s, const char* buf, const int size)
{
    int sz = size;
    const char *p = buf;
    while (sz) {
	int ret = TEMP_FAILURE_RETRY(write(s, p, sz));
	if (ret == -1)
	    return ret;

	p += ret;
	sz -= ret;
    }

    return size;
}

void goon(int a, int b)
{
    char buffer[4096];

    fd_set set;
    while (1) {
	FD_ZERO(&set);
	if (a >= 0)
	    FD_SET(a, &set);
	FD_SET(b, &set);

	int ret = TEMP_FAILURE_RETRY(select((a >? b) + 1, &set, 0, 0, 0));
	if (ret == -1)
	    throw runtime_error("error in select: " + string(strerror(errno)));

	if (a >= 0 && FD_ISSET(a, &set)) {
	    int sz = TEMP_FAILURE_RETRY(read(a, buffer, 4096));
	    if (sz == -1)
		throw runtime_error("error in read: " + string(strerror(errno)));
	    else if (!sz && !a) {
		if (b >= 0)
		    shutdown(b, SHUT_WR);
		a = -1;
	    } else if (!sz)
		break;

	    sz = sendall(b, buffer, sz);
	    if (sz == -1)
		throw runtime_error("error in write: " + string(strerror(errno)));
	}

	if (FD_ISSET(b, &set)) {
	    int sz = TEMP_FAILURE_RETRY(read(b, buffer, 4096));
	    if (sz == -1)
		throw runtime_error("error in read: " + string(strerror(errno)));
	    else if (!sz)
		break;

	    sz = sendall((a>0)?a:1, buffer, sz);
	    if (sz == -1)
		throw runtime_error("error in write: " + string(strerror(errno)));
	}
    }
}
