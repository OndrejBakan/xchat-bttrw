#include <iostream>
#include <sstream>
#include <stdexcept>
#include <ctime>
#include <memory>
#include <map>
#include <openssl/md5.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <typeinfo>
#include "xchat.h"
#include "irc.h"
#include "TomiTCP/net.h"
#include "TomiTCP/str.h"
using namespace std;
using namespace xchat;
using namespace net;

#define VERSION "SVN"

/*
 * Hash nick to make unique username
 */
string hash(string s)
{
    strtolower(s);
    unsigned char mdbuf[16]; char tmp[10];
    MD5_CTX md5;
    MD5_Init(&md5);
    MD5_Update(&md5, s.c_str(), s.length());
    MD5_Final(mdbuf, &md5);
    sprintf(tmp, "%.8x", *((unsigned int*)mdbuf));
    return tmp;
}

/*
 * Some global variables
 */
TomiTCP s;
auto_ptr<TomiTCP> c;
auto_ptr<XChat> x;

const char * const me = "xchat.cz";
const char * const sexhost[] = {
    "girls.xchat.cz",
    "boys.xchat.cz",
    "users.xchat.cz"
};

/*
 * Get a host for user based on his/her sex
 */
const char * getsexhost(string src)
{
    x_nick *n = x->findnick(src, 0);
    if (n)
	return sexhost[n->sex];

    return sexhost[2];
}

void sigchld(int) {
    wait(0);
}

int main(int argc, char *argv[])
{
    xchat_init();
    signal(SIGCHLD, sigchld);

    int port = 6669;
    if (argc == 2 && atol(argv[1]))
	port = atol(argv[1]);

    try {
	s.listen(port);
main_accept:
	c.reset(s.accept());

	pid_t pid = fork();
	if (pid < 0)
	    return -1;
	if (pid > 0) {
	    c.reset(0);
	    goto main_accept;
	}

	string nick, pass;

	while (1) {
	    if (input_timeout(c->sock, 1000) > 0) {
		/*
		 * Got message from IRC client
		 */
		string l, prefix;
		vector<string> cmd;
		if (!c->getline(l))
		    break;
	       	chomp(l);

		parsein(l, prefix, cmd);
		if (!cmd.size()) continue;

		strtoupper(cmd[0]);

		//cout << l << endl;

		/*
		 * User registration
		 */
		if (!x.get() && cmd[0] == "NICK" && cmd.size() >= 2) {
		    nick = cmd[1];
		} else if (!x.get() && cmd[0] == "PASS" && cmd.size() >= 2) {
		    pass = cmd[1];
		} else if (!x.get() && cmd[0] == "USER" && cmd.size() > 1) {
		    if (!nick.length() || !pass.length()) {
			fprintf(*c, ":%s ERROR :Need password and nick!\n", me);
			break;
		    }

		    try { x.reset(new XChat(nick, pass)); }
		    catch (runtime_error e) {
			fprintf(*c, ":%s ERROR :%s\n", me, e.what());
			break;
		    }

		    /*
		     * Successful login, so output some numerics to make
		     * client happy.
		     */

		    fprintf(*c, ":%s 001 %s :Welcome Back To The Real World, but still connected to xchat.cz\n", me, nick.c_str());
		    fprintf(*c, ":%s 002 %s :Your host is %s[%s/%i]"
			    ", running version xchat-bttrw " VERSION "\n", me,
			    nick.c_str(), me, revers(c->lname).c_str(), port);
		    fprintf(*c, ":%s 003 %s :This server was created god knows when\n",
			    me, nick.c_str());
		    fprintf(*c, ":%s 004 %s :%s xchat-bttrw-" VERSION " 0 0\n",
			    me, nick.c_str(), me);
		    fprintf(*c, ":%s 005 %s :MODES=1 MAXTARGETS=1 NICKLEN=256\n", me, nick.c_str());
		    fprintf(*c, ":%s 005 %s :CHANTYPES=# PREFIX=ov(@+) CHANMODES=,,,"
			    " NETWORK=xchat.cz CASEMAPPING=ascii\n", me, nick.c_str());
		} else if (!x.get()) {
		    /*
		     * If the user is not registered and sends some other
		     * command, quit him.
		     */
		    fprintf(*c, ":%s ERROR :Not registered\n", me);
		    break;

		    /*
		     * And here follow commands, which can be invoked after
		     * successful registration.
		     */
		} else if (cmd[0] == "SET" && cmd.size() >= 2) {
		    strtoupper(cmd[1]);
		    if (cmd[1] == "IDLE_INTERVAL" && cmd.size() == 3) {
			idle_interval = atol(cmd[2].c_str());
			fprintf(*c, ":%s NOTICE %s :idle_interval set to %i\n",
				me, nick.c_str(), idle_interval);
		    } else if (cmd[1] == "CHARSET" && cmd.size() == 3) {
			client_charset = cmd[2];
			fprintf(*c, ":%s NOTICE %s :client_charset set to %s\n",
				me, nick.c_str(), client_charset.c_str());
		    } else {
			fprintf(*c, ":%s NOTICE %s :Bad variable or parameter"
				" count\n", me, nick.c_str());
		    }
		} else if (cmd[0] == "PING") {
		    if (cmd.size() >= 2) {
			fprintf(*c, ":%s PONG :%s\n", me, cmd[1].c_str());
		    } else {
			fprintf(*c, ":%s PONG %s\n", me, me);
		    }
		} else if (cmd[0] == "QUIT") {
		    break;
		} else if (cmd[0] == "JOIN" && cmd.size() >= 2) {
		    stringstream s(cmd[1]);
		    string chan;

		    /*
		     * Join comma separated list of channels
		     */
		    while (getline(s, chan, ',')) {
			if (chan[0] == '#')
			    chan.erase(chan.begin());

			try {
			    x->join(chan);

			    fprintf(*c, ":%s!%s@%s JOIN #%s\n", nick.c_str(), hash(nick).c_str(),
				    getsexhost(nick), chan.c_str());

			    // output userlist (NAMES)
			    string tmp; int i; nicklist_t::iterator j;
			    for (i = 1, j = x->rooms[chan].nicklist.begin();
				    j != x->rooms[chan].nicklist.end(); j++, i++) {
				tmp += ((j->first == x->rooms[chan].admin)?"@":"") +
				    j->second.nick + " ";
				if (i % 5 == 0) {
				    fprintf(*c, ":%s 353 %s = #%s :%s\n", me, nick.c_str(),
					    chan.c_str(), tmp.c_str());
				    tmp.clear();
				}
			    }
			    if (tmp.length()) {
				fprintf(*c, ":%s 353 %s = #%s :%s\n", me, nick.c_str(),
					chan.c_str(), tmp.c_str());
			    }
			    fprintf(*c, ":%s 366 %s #%s :End of /NAMES list.\n", me,
				    nick.c_str(), chan.c_str());
			} catch (runtime_error e) {
			    fprintf(*c, ":%s 403 %s #%s :%s\n", me, nick.c_str(),
				    chan.c_str(), e.what());
			}
		    }
		} else if (cmd[0] == "PART" && cmd.size() >= 2) {
		    stringstream s(cmd[1]);
		    string chan;

		    /*
		     * Part comma separated list of channels
		     */
		    while (getline(s, chan, ',')) {
			if (chan[0] == '#')
			    chan.erase(chan.begin());

			if (x->rooms.find(chan) != x->rooms.end()) {
			    try {
				x->leave(chan);
				fprintf(*c, ":%s!%s@%s PART #%s :\n", nick.c_str(),
					hash(nick).c_str(), getsexhost(nick),
					chan.c_str());
			    } catch (runtime_error e) {
				fprintf(*c, ":%s NOTICE %s :Error: %s\n", me,
					nick.c_str(), e.what());
			    }
			} else {
			    fprintf(*c, ":%s 403 %s %s :No such channel\n", me,
				    nick.c_str(), chan.c_str());
			}
		    }
		} else if ((cmd[0] == "PRIVMSG" || cmd[0] == "NOTICE") && cmd.size() == 3) {
		    if (cmd[0] == "NOTICE")
			cmd[1] = "Notice: " + cmd[1];

		    if (cmd[1][0] == '#') {
			cmd[1].erase(cmd[1].begin());

			/*
			 * Channel message
			 */
			x->msg(cmd[1], cmd[2]);
		    } else {
			/*
			 * Private message
			 */
			try { x->whisper(cmd[1], cmd[2]); }
			catch (runtime_error e) {
			    fprintf(*c, ":%s NOTICE %s :Error: %s\n", me,
				    nick.c_str(), e.what());
			}
		    }
		} else if (cmd[0] == "MODE" && cmd.size() == 2) {
		    // just to make client's `channel synchronizing' happy
		    fprintf(*c, ":%s 324 %s %s +\n", me, nick.c_str(), cmd[1].c_str());
		} else if (cmd[0] == "MODE" && cmd.size() == 3 && cmd[2][0] == 'b') {
		    // just to make client's `channel synchronizing' happy
		    fprintf(*c, ":%s 368 %s %s :End of Channel Ban List\n", me,
			    nick.c_str(), cmd[1].c_str());
		} else if (cmd[0] == "WHO" && cmd.size() == 2) {
		    if (cmd[1][0] == '#') {
			cmd[1].erase(cmd[1].begin());

			/*
			 * Output channel WHO
			 */
			for (nicklist_t::iterator i = x->rooms[cmd[1]].nicklist.begin();
				i != x->rooms[cmd[1]].nicklist.end(); i++) {
			    fprintf(*c, ":%s 352 %s #%s %s %s %s %s %s :%d %s\n", me,
				    nick.c_str(), cmd[1].c_str(), hash(i->second.nick).c_str(),
				    sexhost[i->second.sex], me, i->second.nick.c_str(), 
				    (i->first == x->rooms[cmd[1]].admin)?"H@":"H",
				    0,
				    "xchat.cz user");
			}
			cmd[1] = "#" + cmd[1];
		    } else {
			/*
			 * Output user WHO
			 */
			x_nick *n = x->findnick(cmd[1], 0);
			if (n)
			    fprintf(*c, ":%s 352 %s %s %s %s %s %s %s :%d %s\n", me,
				    nick.c_str(), "*", hash(n->nick).c_str(),
				    sexhost[n->sex], me, n->nick.c_str(), "H", 0,
				    "xchat.cz user");
		    }
		    fprintf(*c, ":%s 315 %s %s :End of /WHO list.\n", me,
			    nick.c_str(), cmd[1].c_str());
		} else if (cmd[0] == "WHOIS" && cmd.size() >= 2) {
		    /*
		     * Mangle `WHOIS nick' into `/info nick'
		     * or
		     * Mangle `WHOIS nick nick' into `/info2 nick'
		     * Also, output a simple WHOIS reply.
		     */
		    if (x->rooms.size()) {
			x->msg(x->rooms.begin()->first,
				string("/info") + ((cmd.size() != 2)?"2 ":" ")
				    + cmd[1]);
		    } else {
			fprintf(*c, ":%s NOTICE %s :Can't do WHOIS "
				"without channel joined\n", me, nick.c_str());
		    }

		    x_nick *n = x->findnick(cmd[1], 0);
		    if (n)
			fprintf(*c, ":%s 311 %s %s %s %s * :%s\n", me,
				nick.c_str(), n->nick.c_str(),
				hash(n->nick).c_str(), sexhost[n->sex],
				"xchat.cz user");
		    fprintf(*c, ":%s 318 %s %s :End of /WHOIS list.\n", me,
			    nick.c_str(), cmd[1].c_str());
		} else {
		    cout << l << endl;
		    fprintf(*c, ":%s NOTICE %s :Unknown command\n", me, nick.c_str());
		}
	    }

	    if (x.get()) {
		/*
		 * Let x do it's job on send queue
		 */
		try { x->do_sendq(); }
		catch (runtime_error e) {
		    fprintf(*c, ":%s NOTICE %s :Error: %s\n", me,
			    nick.c_str(), e.what());
		}

		/*
		 * Receive some data sometimes
		 */
		try { x->fill_recvq(); }
		catch (runtime_error e) {
		    fprintf(*c, ":%s NOTICE %s :Error: %s\n", me,
			    nick.c_str(), e.what());
		}
	    }

	    /*
	     * Go through recv queue and process messages
	     */
	    while (x.get() && !x->recvq.empty()) {
		auto_ptr<Event> e(x->recvq_pop());
		//cout << typeid(*(e.get())).name() << endl;

		if (dynamic_cast<EvRoomError*>(e.get())) {
		    auto_ptr<EvRoomError> f((EvRoomError*)e.release());

		    try { x->leave(f->getrid()); } catch (...) { }
		    fprintf(*c, ":%s!%s@%s PART #%s :\n", nick.c_str(),
			    hash(nick).c_str(), getsexhost(nick), f->getrid().c_str());
		    fprintf(*c, ":%s NOTICE %s :Error: %s\n", me,
			    nick.c_str(), f->str().c_str());
		} else if (dynamic_cast<EvRoomMsg*>(e.get())) {
		    auto_ptr<EvRoomMsg> f((EvRoomMsg*)e.release());

		    fprintf(*c, ":%s!%s@%s PRIVMSG #%s :%s\n", f->getsrc().c_str(),
			    hash(f->getsrc()).c_str(), getsexhost(f->getsrc()),
			    f->getrid().c_str(), f->str().c_str());
		} else if (dynamic_cast<EvRoomWhisper*>(e.get())) {
		    auto_ptr<EvRoomWhisper> f((EvRoomWhisper*)e.release());

		    fprintf(*c, ":%s!%s@%s PRIVMSG %s :%s\n", f->getsrc().c_str(),
			    hash(f->getsrc()).c_str(), getsexhost(f->getsrc()),
			    f->gettarget().c_str(), f->str().c_str());
		} else if (dynamic_cast<EvRoomJoin*>(e.get())) {
		    auto_ptr<EvRoomJoin> f((EvRoomJoin*)e.release());

		    fprintf(*c, ":%s!%s@%s JOIN #%s\n", f->getsrc().nick.c_str(),
			    hash(f->getsrc().nick).c_str(), sexhost[f->getsrc().sex],
			    f->getrid().c_str());
		} else if (dynamic_cast<EvRoomLeave*>(e.get())) {
		    auto_ptr<EvRoomLeave> f((EvRoomLeave*)e.release());

		    fprintf(*c, ":%s!%s@%s PART #%s :%s\n", f->getsrc().nick.c_str(),
			    hash(f->getsrc().nick).c_str(), sexhost[f->getsrc().sex],
			    f->getrid().c_str(), f->getreason().c_str());
		} else if (dynamic_cast<EvRoomKick*>(e.get())) {
		    auto_ptr<EvRoomKick> f((EvRoomKick*)e.release());

		    fprintf(*c, ":%s!%s@%s KICK #%s %s :%s\n", f->getsrc().c_str(),
			    hash(f->getsrc()).c_str(), getsexhost(f->getsrc()),
			    f->getrid().c_str(), f->gettarget().nick.c_str(),
			    f->getreason().c_str());
		} else if (dynamic_cast<EvRoomSysMsg*>(e.get())) {
		    auto_ptr<EvRoomSysMsg> f((EvRoomSysMsg*)e.release());

		    fprintf(*c, ":%s NOTICE #%s :System: %s\n", me,
			    f->getrid().c_str(), f->str().c_str());
		} else if (dynamic_cast<EvRoomIdlerMsg*>(e.get())) {
		    auto_ptr<EvRoomIdlerMsg> f((EvRoomIdlerMsg*)e.release());

		    fprintf(*c, ":%s NOTICE #%s :System: %s [IDLER]\n", me,
			    f->getrid().c_str(), f->str().c_str());
		} else if (dynamic_cast<EvRoomSysText*>(e.get())) {
		    auto_ptr<EvRoomSysText> f((EvRoomSysText*)e.release());

		    fprintf(*c, ":%s NOTICE #%s :%s\n", me,
			    f->getrid().c_str(), f->str().c_str());
		} else if (dynamic_cast<EvRoomAdminChange*>(e.get())) {
		    auto_ptr<EvRoomAdminChange> f((EvRoomAdminChange*)e.release());

		    fprintf(*c, ":%s MODE #%s -o+o %s %s\n", me,
			    f->getrid().c_str(), f->getbefore().c_str(),
			    f->getnow().c_str());
		} else if (dynamic_cast<EvRoomAdvert*>(e.get())) {
		    auto_ptr<EvRoomAdvert> f((EvRoomAdvert*)e.release());

		    fprintf(*c, ":%s NOTICE #%s :Advert: %s [ %s ]\n", me,
			    f->getrid().c_str(), f->str().c_str(),
			    f->getlink().c_str());
		} else if (dynamic_cast<EvRoomOther*>(e.get())) {
		    auto_ptr<EvRoomOther> f((EvRoomOther*)e.release());

		    fprintf(*c, ":%s NOTICE #%s :Other: %s - %s\n", me,
			    f->getrid().c_str(), typeid(*(f.get())).name(),
			    f->str().c_str());
		} else {
		    fprintf(*c, ":%s NOTICE %s :Other: %s - %s\n", me,
			    nick.c_str(), typeid(*(e.get())).name(),
			    e->str().c_str());
		}
	    }
	}
    } catch (runtime_error e) {
	cerr << e.what() << endl;
    }

    return 0;
}