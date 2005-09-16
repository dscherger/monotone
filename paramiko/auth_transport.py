# Copyright (C) 2003-2005 Robey Pointer <robey@lag.net>
#
# This file is part of paramiko.
#
# Paramiko is free software; you can redistribute it and/or modify it under the
# terms of the GNU Lesser General Public License as published by the Free
# Software Foundation; either version 2.1 of the License, or (at your option)
# any later version.
#
# Paramiko is distrubuted in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
# details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with Paramiko; if not, write to the Free Software Foundation, Inc.,
# 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.

"""
L{Transport} is a subclass of L{BaseTransport} that handles authentication.
This separation keeps either class file from being too unwieldy.
"""

import threading

# this helps freezing utils
import encodings.utf_8

from common import *
import util
from transport import BaseTransport
from message import Message
from ssh_exception import SSHException, BadAuthenticationType, PartialAuthentication


class Transport (BaseTransport):
    """
    An SSH Transport attaches to a stream (usually a socket), negotiates an
    encrypted session, authenticates, and then creates stream tunnels, called
    L{Channel}s, across the session.  Multiple channels can be multiplexed
    across a single session (and often are, in the case of port forwardings).
    """
    
    def __init__(self, sock):
        BaseTransport.__init__(self, sock)
        self.username = None
        self.authenticated = False
        self.auth_event = None
        self.auth_method = ''
        self.password = None
        self.private_key = None
        # for server mode:
        self.auth_username = None
        self.auth_fail_count = 0

    def __repr__(self):
        out = '<paramiko.Transport at %s' % hex(long(id(self)) & 0xffffffffL)
        if not self.active:
            out += ' (unconnected)'
        else:
            if self.local_cipher != '':
                out += ' (cipher %s, %d bits)' % (self.local_cipher,
                                                  self._cipher_info[self.local_cipher]['key-size'] * 8)
            if self.authenticated:
                if len(self.channels) == 1:
                    out += ' (active; 1 open channel)'
                else:
                    out += ' (active; %d open channels)' % len(self.channels)
            elif self.initial_kex_done:
                out += ' (connected; awaiting auth)'
            else:
                out += ' (connecting)'
        out += '>'
        return out

    def is_authenticated(self):
        """
        Return true if this session is active and authenticated.

        @return: True if the session is still open and has been authenticated successfully;
        False if authentication failed and/or the session is closed.
        @rtype: bool
        """
        return self.authenticated and self.active

    def get_username(self):
        """
        Return the username this connection is authenticated for.  If the
        session is not authenticated (or authentication failed), this method
        returns C{None}.

        @return: username that was authenticated, or C{None}.
        @rtype: string

        @since: fearow
        """
        if self.server_mode:
            return self.auth_username
        else:
            return self.username

    def auth_publickey(self, username, key, event=None):
        """
        Authenticate to the server using a private key.  The key is used to
        sign data from the server, so it must include the private part.
        
        If an C{event} is passed in, this method will return immediately, and
        the event will be triggered once authentication succeeds or fails.  On
        success, L{is_authenticated} will return C{True}.  On failure, you may
        use L{get_exception} to get more detailed error information.
        
        Since 1.1, if no event is passed, this method will block until the
        authentication succeeds or fails.  On failure, an exception is raised.
        Otherwise, the method simply returns.

        If the server requires multi-step authentication (which is very rare),
        this method will return a list of auth types permissible for the next
        step.  Otherwise, in the normal case, an empty list is returned.

        @param username: the username to authenticate as.
        @type username: string
        @param key: the private key to authenticate with.
        @type key: L{PKey <pkey.PKey>}
        @param event: an event to trigger when the authentication attempt is
        complete (whether it was successful or not)
        @type event: threading.Event
        @return: list of auth types permissible for the next stage of
            authentication (normally empty).
        @rtype: list
        
        @raise BadAuthenticationType: if public-key authentication isn't
            allowed by the server for this user (and no event was passed in).
        @raise SSHException: if the authentication failed (and no event was
            passed in).
        """
        if (not self.active) or (not self.initial_kex_done):
            # we should never try to authenticate unless we're on a secure link
            raise SSHException('No existing session')
        if event is None:
            my_event = threading.Event()
        else:
            my_event = event
        self.lock.acquire()
        try:
            self.auth_event = my_event
            self.auth_method = 'publickey'
            self.username = username
            self.private_key = key
            self._request_auth()
        finally:
            self.lock.release()
        if event is not None:
            # caller wants to wait for event themselves
            return []
        return self._wait_for_response(my_event)

    def auth_password(self, username, password, event=None):
        """
        Authenticate to the server using a password.  The username and password
        are sent over an encrypted link.
        
        If an C{event} is passed in, this method will return immediately, and
        the event will be triggered once authentication succeeds or fails.  On
        success, L{is_authenticated} will return C{True}.  On failure, you may
        use L{get_exception} to get more detailed error information.

        Since 1.1, if no event is passed, this method will block until the
        authentication succeeds or fails.  On failure, an exception is raised.
        Otherwise, the method simply returns.
        
        If the server requires multi-step authentication (which is very rare),
        this method will return a list of auth types permissible for the next
        step.  Otherwise, in the normal case, an empty list is returned.
        
        @param username: the username to authenticate as
        @type username: string
        @param password: the password to authenticate with
        @type password: string
        @param event: an event to trigger when the authentication attempt is
            complete (whether it was successful or not)
        @type event: threading.Event
        @return: list of auth types permissible for the next stage of
            authentication (normally empty)
        @rtype: list
        
        @raise BadAuthenticationType: if password authentication isn't
            allowed by the server for this user (and no event was passed in)
        @raise SSHException: if the authentication failed (and no event was
            passed in)
        """
        if (not self.active) or (not self.initial_kex_done):
            # we should never try to send the password unless we're on a secure link
            raise SSHException('No existing session')
        if event is None:
            my_event = threading.Event()
        else:
            my_event = event
        self.lock.acquire()
        try:
            self.auth_event = my_event
            self.auth_method = 'password'
            self.username = username
            self.password = password
            self._request_auth()
        finally:
            self.lock.release()
        if event is not None:
            # caller wants to wait for event themselves
            return []
        return self._wait_for_response(my_event)


    ###  internals...


    def _request_auth(self):
        m = Message()
        m.add_byte(chr(MSG_SERVICE_REQUEST))
        m.add_string('ssh-userauth')
        self._send_message(m)

    def _disconnect_service_not_available(self):
        m = Message()
        m.add_byte(chr(MSG_DISCONNECT))
        m.add_int(DISCONNECT_SERVICE_NOT_AVAILABLE)
        m.add_string('Service not available')
        m.add_string('en')
        self._send_message(m)
        self.close()

    def _disconnect_no_more_auth(self):
        m = Message()
        m.add_byte(chr(MSG_DISCONNECT))
        m.add_int(DISCONNECT_NO_MORE_AUTH_METHODS_AVAILABLE)
        m.add_string('No more auth methods available')
        m.add_string('en')
        self._send_message(m)
        self.close()

    def _get_session_blob(self, key, service, username):
        m = Message()
        m.add_string(self.session_id)
        m.add_byte(chr(MSG_USERAUTH_REQUEST))
        m.add_string(username)
        m.add_string(service)
        m.add_string('publickey')
        m.add_boolean(1)
        m.add_string(key.get_name())
        m.add_string(str(key))
        return str(m)

    def _wait_for_response(self, event):
        while True:
            event.wait(0.1)
            if not self.active:
                e = self.get_exception()
                if e is None:
                    e = SSHException('Authentication failed.')
                raise e
            if event.isSet():
                break
        if not self.is_authenticated():
            e = self.get_exception()
            if e is None:
                e = SSHException('Authentication failed.')
            # this is horrible.  python Exception isn't yet descended from
            # object, so type(e) won't work. :(
            if issubclass(e.__class__, PartialAuthentication):
                return e.allowed_types
            raise e
        return []

    def _parse_service_request(self, m):
        service = m.get_string()
        if self.server_mode and (service == 'ssh-userauth'):
            # accepted
            m = Message()
            m.add_byte(chr(MSG_SERVICE_ACCEPT))
            m.add_string(service)
            self._send_message(m)
            return
        # dunno this one
        self._disconnect_service_not_available()

    def _parse_service_accept(self, m):
        service = m.get_string()
        if service == 'ssh-userauth':
            self._log(DEBUG, 'userauth is OK')
            m = Message()
            m.add_byte(chr(MSG_USERAUTH_REQUEST))
            m.add_string(self.username)
            m.add_string('ssh-connection')
            m.add_string(self.auth_method)
            if self.auth_method == 'password':
                m.add_boolean(False)
                m.add_string(self.password.encode('UTF-8'))
            elif self.auth_method == 'publickey':
                m.add_boolean(True)
                m.add_string(self.private_key.get_name())
                m.add_string(str(self.private_key))
                blob = self._get_session_blob(self.private_key, 'ssh-connection', self.username)
                sig = self.private_key.sign_ssh_data(self.randpool, blob)
                m.add_string(str(sig))
            else:
                raise SSHException('Unknown auth method "%s"' % self.auth_method)
            self._send_message(m)
        else:
            self._log(DEBUG, 'Service request "%s" accepted (?)' % service)

    def _parse_userauth_request(self, m):
        if not self.server_mode:
            # er, uh... what?
            m = Message()
            m.add_byte(chr(MSG_USERAUTH_FAILURE))
            m.add_string('none')
            m.add_boolean(0)
            self._send_message(m)
            return
        if self.authenticated:
            # ignore
            return
        username = m.get_string()
        service = m.get_string()
        method = m.get_string()
        self._log(DEBUG, 'Auth request (type=%s) service=%s, username=%s' % (method, service, username))
        if service != 'ssh-connection':
            self._disconnect_service_not_available()
            return
        if (self.auth_username is not None) and (self.auth_username != username):
            self._log(WARNING, 'Auth rejected because the client attempted to change username in mid-flight')
            self._disconnect_no_more_auth()
            return
        self.auth_username = username

        if method == 'none':
            result = self.server_object.check_auth_none(username)
        elif method == 'password':
            changereq = m.get_boolean()
            password = m.get_string().decode('UTF-8', 'replace')
            if changereq:
                # always treated as failure, since we don't support changing passwords, but collect
                # the list of valid auth types from the callback anyway
                self._log(DEBUG, 'Auth request to change passwords (rejected)')
                newpassword = m.get_string().decode('UTF-8', 'replace')
                result = AUTH_FAILED
            else:
                result = self.server_object.check_auth_password(username, password)
        elif method == 'publickey':
            sig_attached = m.get_boolean()
            keytype = m.get_string()
            keyblob = m.get_string()
            try:
                key = self._key_info[keytype](Message(keyblob))
            except SSHException, e:
                self._log(INFO, 'Auth rejected: public key: %s' % str(e))
                key = None
            except:
                self._log(INFO, 'Auth rejected: unsupported or mangled public key')
                key = None
            if key is None:
                self._disconnect_no_more_auth()
                return
            # first check if this key is okay... if not, we can skip the verify
            result = self.server_object.check_auth_publickey(username, key)
            if result != AUTH_FAILED:
                # key is okay, verify it
                if not sig_attached:
                    # client wants to know if this key is acceptable, before it
                    # signs anything...  send special "ok" message
                    m = Message()
                    m.add_byte(chr(MSG_USERAUTH_PK_OK))
                    m.add_string(keytype)
                    m.add_string(keyblob)
                    self._send_message(m)
                    return
                sig = Message(m.get_string())
                blob = self._get_session_blob(key, service, username)
                if not key.verify_ssh_sig(blob, sig):
                    self._log(INFO, 'Auth rejected: invalid signature')
                    result = AUTH_FAILED
        else:
            result = self.server_object.check_auth_none(username)
        # okay, send result
        m = Message()
        if result == AUTH_SUCCESSFUL:
            self._log(INFO, 'Auth granted (%s).' % method)
            m.add_byte(chr(MSG_USERAUTH_SUCCESS))
            self.authenticated = True
        else:
            self._log(INFO, 'Auth rejected (%s).' % method)
            m.add_byte(chr(MSG_USERAUTH_FAILURE))
            m.add_string(self.server_object.get_allowed_auths(username))
            if result == AUTH_PARTIALLY_SUCCESSFUL:
                m.add_boolean(1)
            else:
                m.add_boolean(0)
                self.auth_fail_count += 1
        self._send_message(m)
        if self.auth_fail_count >= 10:
            self._disconnect_no_more_auth()

    def _parse_userauth_success(self, m):
        self._log(INFO, 'Authentication successful!')
        self.authenticated = True
        if self.auth_event != None:
            self.auth_event.set()

    def _parse_userauth_failure(self, m):
        authlist = m.get_list()
        partial = m.get_boolean()
        if partial:
            self._log(INFO, 'Authentication continues...')
            self._log(DEBUG, 'Methods: ' + str(authlist))
            self.saved_exception = PartialAuthentication(authlist)
        elif self.auth_method not in authlist:
            self._log(INFO, 'Authentication type not permitted.')
            self._log(DEBUG, 'Allowed methods: ' + str(authlist))
            self.saved_exception = BadAuthenticationType('Bad authentication type', authlist)
        else:
            self._log(INFO, 'Authentication failed.')
        self.authenticated = False
        self.username = None
        if self.auth_event != None:
            self.auth_event.set()

    def _parse_userauth_banner(self, m):
        banner = m.get_string()
        lang = m.get_string()
        self._log(INFO, 'Auth banner: ' + banner)
        # who cares.

    _handler_table = BaseTransport._handler_table.copy()
    _handler_table.update({
        MSG_SERVICE_REQUEST: _parse_service_request,
        MSG_SERVICE_ACCEPT: _parse_service_accept,
        MSG_USERAUTH_REQUEST: _parse_userauth_request,
        MSG_USERAUTH_SUCCESS: _parse_userauth_success,
        MSG_USERAUTH_FAILURE: _parse_userauth_failure,
        MSG_USERAUTH_BANNER: _parse_userauth_banner,
        })

