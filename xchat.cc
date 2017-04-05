#include <cstdio>
#include <cstdlib>
#include <stdbool.h>
#include <iostream>
#include <string>
#include <set>
#include <clocale>
#include "xchat.h"
#include "smiles.h"
#include "idle.h"
#include "TomiTCP/http.h"
#include "TomiTCP/str.h"
using namespace net;

namespace xchat {
    /**
     * Call all initializations needed for libxchat-bttrw.
     * That involves HTML recoder init and setlocale.
     */
    void xchat_init()
    {
	net::TomiHTTP::g_http_conn_timeout = 30;

	init_recode();
#ifdef WIN32
	setlocale(LC_ALL, "Czech.UTF-8");
#else
	setlocale(LC_ALL, "cs_CZ.UTF-8");
#endif
    }

    /**
     * Find a nick structure in rooms we are in.
     * \param src Searched nick.
     * \param r The room in which we found it is returned here, if non-NULL.
     * \return Pointer to x_nick or NULL if not found.
     */
    x_nick* XChat::findnick(string src, room **r)
    {
	strtolower(src);

	for (rooms_t::iterator i = rooms.begin(); i != rooms.end(); i++) {
	    nicklist_t::iterator n = i->second.nicklist.find(src);
	    if (n != i->second.nicklist.end()) {
		if (r)
		    *r = &i->second;
		return &n->second;
	    }
	}

	if (strtolower_nr(me.nick) == src) {
	    static room tmpr;
	    if (r)
		*r = &tmpr;
	    return &me;
	}

	return 0;
    }

    /**
     * PARSE CSRF - because Gym
     * \param rid Room ID.
     * \return token hash from textarea
     */

    string XChat::gettoken(const string& rid)
    {

      XChatAPI s;
      string l;

      try {

	    int ret = request_GET(s, SERVER_MODCHAT,
		    "modchat?op=textpageng&skin=2&js=1&rid=" + rid, PATH_AUTH);
	    if (ret != 200)
		  throw runtime_error("Not HTTP 200 Ok while posting msg");
	   } catch (runtime_error &e) {
	    throw runtime_error(string(e.what()) + " - " + lastsrv_broke());
	   }

     while(s.getline(l))
     {

      static string pat4 = " name=\"wtkn\"";
      string::size_type wtkn;
      if ((wtkn = l.find(pat4)) != string::npos)
      {

        static const string VALUE = "value";
        static const char DOUBLE_QUOTE = '"';

        string result;

        size_t pos = l.rfind(VALUE);
        if (pos != string::npos)
        {
            size_t beg = l.find_first_of(DOUBLE_QUOTE, pos);

            if (beg != string::npos)
            {
                size_t end = l.find_first_of(DOUBLE_QUOTE, beg + 1);

                if (end != string::npos)
                {

                  result = l.substr(beg + 1, end - beg - 1);
                  return result;

                }

            }

        }

      }

     }

     return "";

    }

    /**
     * Go through #sendq and send messages, take flood protection and
     * #max_msg_length into account.  Then, send anti-idle messages if
     * necessary.
     */
    void XChat::do_sendq()
    {
	if (!sendq.empty() && time(0) - last_sent >= send_interval) {
	    send_item &ref = sendq.front(), msg = ref, left = ref;
	    string prepend;

	    /*
	     * Handle whisper with unknown room
	     */
	    if (ref.room.empty()) {
		if (!rooms.size()) {
		    sendq.pop_front();
		    throw runtime_error("Can't send PRIVMSG's without channel joined");
		}

		/*
		 * Choose room to send global msg
		 */
		room *r;
		x_nick *n = findnick(ref.target, &r);
		if (n) {
		    msg.room = r->rid;
		} else {
		    msg.room = rooms.begin()->first;
		}
		prepend = "/m " + msg.target + " ";
		msg.target = "~";
	    }

	    /*
	     * Look if we have to split the message
	     */
	    if (u8strlen(ref.msg.c_str()) + u8strlen(prepend.c_str()) > max_msg_length) {
		if (ref.msg.length() && ref.msg[0] == '/') {
		    auto_ptr<EvRoomError> ev(new EvRoomError);
		    ev->s = "Message might have been shortened";
		    ev->rid = ref.room;
		    ev->fatal = false;
		    recvq_push((auto_ptr<Event>) ev);
		} else {
		    if (u8strlen(prepend.c_str()) >= max_msg_length) {
			sendq.pop_front();
			throw runtime_error("Fuck... this should have never happened!");
		    }

		    int split = u8strlimit(ref.msg.c_str(),
			    max_msg_length - u8strlen(prepend.c_str()));
		    msg.msg.erase(split);
		}
	    }

	    left.msg.erase(0, msg.msg.length());

	    /*
	     * Fix messages splitted before '/' char
	     */
	    if (left.msg.length() && left.msg[0] == '/') {
		left.msg.insert(0, " ");
	    }

	    /*
	     * Post it
	     */
	    if (rooms.find(msg.room) != rooms.end()) {
		if (putmsg(rooms[msg.room], msg.target, prepend + msg.msg, gettoken(msg.room))) {
		    if (!ref.retries--) {
			auto_ptr<EvRoomError> ev(new EvRoomError);
			ev->s = "Message lost, xchat is not willing to let it go";
			ev->rid = msg.room;
			ev->fatal = false;
			recvq_push((auto_ptr<Event>) ev);
		    } else {
			auto_ptr<EvRoomError> ev(new EvRoomError);
			ev->s = "Reposting msg, xchat refused it";
			ev->rid = msg.room;
			ev->fatal = false;
			recvq_push((auto_ptr<Event>) ev);
			return;
		    }
		}
	    } else {
		auto_ptr<EvRoomError> ev(new EvRoomError);
		ev->s = "Message lost, room is not available";
		ev->rid = msg.room;
		ev->fatal = false;
		recvq_push((auto_ptr<Event>) ev);
	    }

	    if (left.msg.length())
		ref.msg = left.msg;
	    else
		sendq.pop_front();
	}

	// f00king idler
	if (idle_interval && sendq.empty())
	    for (rooms_t::iterator i = rooms.begin(); i != rooms.end(); i++) {
		if (time(0) - i->second.last_sent >= idle_interval - idle_delta) {
		    sendq.push_back(send_item(i->first, me.nick, genidle()));
		    if (idle_interval >= 5)
			idle_delta = rand() % (idle_interval / 5);
		    else
			idle_delta = 0;
		}
	    }
    }

    /**
     * Get new messages and/or reload room info, if it's the time.
     */
    void XChat::fill_recvq()
    {
	if (time(0) - last_recv >= recv_interval) {
	    set<string> srooms;
	    for (rooms_t::iterator i = rooms.begin(); i != rooms.end(); i++) {
		srooms.insert(i->first);
	    }
	    for (set<string>::iterator i = srooms.begin(); i != srooms.end(); i++) {
		try { getmsg(rooms[*i]); }
		catch (runtime_error &e) {
		    auto_ptr<EvRoomError> f(new EvRoomError);
		    f->s = e.what();
		    f->rid = *i;
		    f->fatal = false;
		    recvq_push((auto_ptr<Event>) f);
		}
	    }

	    last_recv = time(0);

	    /*
	     * Clear the secondary queue.
	     */
	    old_recvq.clear();
	}

	// roominfo reload
	for (rooms_t::iterator i = rooms.begin(); i != rooms.end(); i++) {
	    if (time(0) - i->second.last_roominfo >= roominfo_interval) {
		room old = i->second;

		try { getroominfo(i->second); }
		catch (runtime_error &e) {
		    auto_ptr<EvRoomError> f(new EvRoomError);
		    f->s = e.what();
		    f->rid = i->first;
		    f->fatal = false;
		    recvq_push((auto_ptr<Event>) f);
                    continue;
		}

		/*
		 * Check for room name and description change (resulting in
		 * one EvRoomTopicChange event)
		 */
		if (old.name != i->second.name || old.desc != i->second.desc ||
		    old.web != i->second.web) {
		    auto_ptr<EvRoomTopicChange> e(new EvRoomTopicChange);
		    e->rid = i->first;
		    e->name = i->second.name;
		    e->desc = i->second.desc;
		    e->web = i->second.web;
		    recvq_push((auto_ptr<Event>) e);
		}

		/*
		 * Check for permanent admins changes
		 */
		vector<string> added, removed;
		set_difference(i->second.admins.begin(), i->second.admins.end(),
			old.admins.begin(), old.admins.end(),
			inserter(added, added.end()));
		set_difference(old.admins.begin(), old.admins.end(),
			i->second.admins.begin(), i->second.admins.end(),
			inserter(removed, removed.end()));
		if (added.size() || removed.size()) {
		    auto_ptr<EvRoomAdminsChange> e(new EvRoomAdminsChange);
		    e->rid = i->first;
		    e->added = added;
		    e->removed = removed;
		    recvq_push((auto_ptr<Event>) e);
		}

		i->second.last_roominfo = time(0);
	    }
	}

	// superadmins reload
	if (time(0) - last_superadmins_reload >= superadmins_reload_interval) {
	    try { reloadsuperadmins(); }
            catch (runtime_error &e) {
                auto_ptr<EvError> f(new EvError);
                f->s = e.what();
                recvq_push((auto_ptr<Event>) f);
            }
	}
    }

    /**
     * \brief Helper class for the duplicate_in_queue function. (Predicate)
     */
    class queue_equal_to {
	private:
	    const Event *e;
	public:
	    explicit queue_equal_to(const Event *e) : e(e) { }

	    bool operator ()(const recv_item &a) {
		return *a.e == *e;
	    }
    };

    /**
     * Check if this event is already in the primary or secondary queue.
     * \param e The event.
     * \return True if it is.
     */
    bool XChat::duplicate_in_queue(Event *e)
    {
	queue_equal_to compare(e);
	if (find_if(recvq.begin(), recvq.end(), compare) != recvq.end())
	    return true;
	if (find_if(old_recvq.begin(), old_recvq.end(), compare) != old_recvq.end())
	    return true;

	return false;
    }

    /**
     * Check if given user is in a specified room.
     * \param rid Room id.
     * \param nick User's nick.
     * \return True if he is.
     */
    bool XChat::isin(const string &rid, string nick) {
	if (rooms.find(rid) == rooms.end())
	    return false;
	strtolower(nick);

	if (rooms[rid].nicklist.find(nick) != rooms[rid].nicklist.end())
	    return true;

	return false;
    }

    /**
     * Check if given user is admin in a specified room.
     * (but not permanent admin nor xchat admin)
     * \param rid Room id.
     * \param nick User's nick.
     * \return True if he is.
     */
    bool XChat::isadmin(const string &rid, string nick) {
	if (rooms.find(rid) == rooms.end())
	    return false;
	strtolower(nick);

	if (rooms[rid].admin == nick)
	    return true;

	return false;
    }

    /**
     * Check if given user is permanent admin in a specified room.
     * (but not admin nor xchat admin)
     * \param rid Room id.
     * \param nick User's nick.
     * \return True if he is.
     */
    bool XChat::ispermadmin(const string &rid, string nick) {
	if (rooms.find(rid) == rooms.end())
	    return false;
	strtolower(nick);

	if (rooms[rid].admins.find(nick) != rooms[rid].admins.end())
	    return true;

	return false;
    }

    /**
     * Check if given user is xchat admin.
     * \param nick User's nick.
     * \return True if he is.
     */
    bool XChat::issuperadmin(string nick) {
	strtolower(nick);

	if(superadmins.find(nick) != superadmins.end())
	    return true;

	return false;
    }
}
