#ifndef _EVENTS_H_INCLUDED
#define _EVENTS_H_INCLUDED

#include <string>
#include <ctime>
#include "cloneable.h"

namespace xchat {
    using namespace std;

    class XChat;
    struct room;
    struct x_nick;

    /*
     * Definition of Event tree. Events don't have any identifier, they are
     * meant to be distinguished by RTTI.
     */

    class Event : public cloneable {
	    friend class XChat;
	protected:
	    string s;
	    string d;
	    /**
	     * \brief Stop this message from appearing duplically? Classes
	     * setting this var to true will have to overload operator ==() and
	     * clone().
	     */
	    bool stopdup;
	public:
	    Event() : stopdup(false) { }
	    virtual const string & str() { return s; }
	    virtual const string & date() { return d; }
	    virtual bool operator == (const Event &e) {
		return typeid(*this) == typeid(e) && s == e.s;
	    }
	    virtual bool operator != (const Event &e) { return !operator == (e); }
	    virtual ~Event() {}
    };

    class EvRoomOther : public Event {
	    friend class XChat;
	protected:
	    string rid;
	public:
	    virtual const string & getrid() { return rid; }
	    virtual ~EvRoomOther() {}
    };

    class EvRoomError : public EvRoomOther {
	    friend class XChat;
	private:
	    string s2;
	    bool fatal;
	public:
	    virtual const string & str() { s2 = rid + ": " + s; return s2; }
	    virtual const bool & isfatal() { return fatal; }
	    virtual ~EvRoomError() {}
    };

    class EvRoomMsg : public EvRoomOther {
	    friend class XChat;
	protected:
	    x_nick src;
	public:
	    virtual const x_nick & getsrc() { return src; }
	    virtual ~EvRoomMsg() {}
    };

    class EvRoomJoin : public EvRoomOther {
	    friend class XChat;
	protected:
	    x_nick src;
	public:
	    virtual const x_nick & getsrc() { return src; }
	    virtual ~EvRoomJoin() {}
    };

    class EvRoomLeave : public EvRoomOther {
	    friend class XChat;
	protected:
	    x_nick src;
	    string reason;
	public:
	    virtual const x_nick & getsrc() { return src; }
	    virtual const string & getreason() { return reason; }
	    virtual ~EvRoomLeave() {}
    };

    class EvRoomKick : public EvRoomOther {
	    friend class XChat;
	protected:
	    x_nick src;
	    x_nick target;
	    string reason;
	public:
	    virtual const x_nick & getsrc() { return src; }
	    virtual const string & getreason() { return reason; }
	    virtual const x_nick & gettarget() { return target; }
	    virtual ~EvRoomKick() {}
    };

    class EvRoomAdvert : public EvRoomOther {
	    friend class XChat;
	protected:
	    string link;
	public:
	    virtual const string & getlink() { return link; }
	    virtual ~EvRoomAdvert() {}
    };

    /**
     * \brief A message from the System nick.
     */
    class EvRoomSysMsg : public EvRoomOther {
	    friend class XChat;
	public:
	    virtual ~EvRoomSysMsg() {}
    };

    /**
     * \brief A system message in the room - in fact, every unparsed text in
     * the room.
     */
    class EvRoomSysText : public EvRoomOther {
	    friend class XChat;
	public:
	    virtual ~EvRoomSysText() {}
    };

    /**
     * \brief A line of history (backlog) shown upon entering the room.
     */
    class EvRoomHistoryMsg : public EvRoomOther {
	    friend class XChat;
	    string src;
	    string target;
	public:
	    virtual const string & getsrc() { return src; }
	    virtual const string & gettarget() { return target; }
	    virtual ~EvRoomHistoryMsg() {}
    };

    class EvRoomIdlerMsg : public EvRoomOther {
	    friend class XChat;
	public:
	    virtual ~EvRoomIdlerMsg() {}
    };

    class EvRoomAdminChange : public EvRoomOther {
	    friend class XChat;
	protected:
	    string before; ///< Nickname of the former admin in lowercase.
	    string now; ///< Nickname of the new admin in lowercase.
	    string s2;
	public:
	    virtual const string & str() {
		s2 = "Admin change: " + before + " => " + now;
		return s2;
	    }
	    virtual const string & getbefore() { return before; }
	    virtual const string & getnow() { return now; }
	    virtual ~EvRoomAdminChange() {}
    };

    class EvRoomAdminsChange : public EvRoomOther {
	    friend class XChat;
	protected:
	    vector<string> added, removed;
	public:
	    virtual const vector<string> & getadded() { return added; }
	    virtual const vector<string> & getremoved() { return removed; }
	    virtual ~EvRoomAdminsChange() {}
    };

    class EvRoomLockChange : public EvRoomOther {
	    friend class XChat;
	protected:
	    bool before, now;
	    string s2;
	public:
	    virtual const string & str() {
		s2 = string("Locked change: ") + (before?"locked":"unlocked") +
		    " => " + (now?"locked":"unlocked");
		return s2;
	    }
	    virtual const bool & getbefore() { return before; }
	    virtual const bool & getnow() { return now; }
	    virtual ~EvRoomLockChange() {}
    };

    class EvRoomTopicChange : public EvRoomOther {
	    friend class XChat;
	protected:
	    string name, desc, web, s2;
	public:
	    virtual const string & str() {
		s2 = string("Topic change: ") + name + " " + desc + " " + web;
		return s2;
	    }
	    virtual const string & getname() { return name; }
	    virtual const string & getdesc() { return desc; }
	    virtual const string & getweb() { return web; }
	    virtual ~EvRoomTopicChange() {}
    };

    class EvSuperAdminsChange : public Event {
	    friend class XChat;
	protected:
	    vector<string> added, removed;
	public:
	    virtual const vector<string> & getadded() { return added; }
	    virtual const vector<string> & getremoved() { return removed; }
	    virtual ~EvSuperAdminsChange() {}
    };

    class EvError : public Event {
	    friend class XChat;
	public:
	    virtual ~EvError() {}
    };

    class EvNeedRelogin : public EvError {
	    friend class XChat;
	public:
	    virtual ~EvNeedRelogin() {}
    };

    class EvSysMsg : public Event {
	    friend class XChat;
	public:
	    virtual EvSysMsg * clone() { return new EvSysMsg(*this); }
	    virtual ~EvSysMsg() {}
    };

    class EvWhisper : public Event {
	    friend class XChat;
	protected:
	    x_nick src;
	    x_nick target;
	public:
	    EvWhisper() { stopdup = true; }
	    virtual const x_nick & getsrc() { return src; }
	    virtual const x_nick & gettarget() { return target; }
	    virtual bool operator == (const EvWhisper &e) {
		return Event::operator == (e) && src == e.src && target == e.target;
	    }
	    virtual EvWhisper * clone() { return new EvWhisper(*this); }
	    virtual ~EvWhisper() {}
    };

    class EvNote : public Event {
	    friend class XChat;
	public:
	    virtual ~EvNote() {}
    };

    class EvKill : public Event {
           friend class XChat;
	protected:
           x_nick src;
           x_nick target;
	public:
            virtual const x_nick & getsrc() { return src; }
            virtual const x_nick & gettarget() { return target; }
	    virtual bool operator == (const EvKill &e) {
	        return Event::operator == (e) && src == e.src && target == e.target;
	    }
	    virtual EvKill * clone() { return new EvKill(*this); }
            virtual ~EvKill() {}
    };
}

#include "xchat.h"

#endif
