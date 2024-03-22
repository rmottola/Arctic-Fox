# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import datetime
import errno
import json
import socket
import time
import types


class Message(object):
    def __init__(self, msgid):
        self.id = msgid

    def __eq__(self, other):
        return self.id == other.id


class Command(Message):
    TYPE = 0

    def __init__(self, msgid, name, params):
        Message.__init__(self, msgid)
        self.name = name
        self.params = params

    def __str__(self):
        return "<Command id=%s, name=%s, params=%s>" % (self.id, self.name, self.params)

    def to_msg(self):
        msg = [Command.TYPE, self.id, self.name, self.params]
        return json.dumps(msg)

    @staticmethod
    def from_msg(payload):
        data = json.loads(payload)
        assert data[0] == Command.TYPE
        cmd = Command(data[1], data[2], data[3])
        return cmd


class Response(Message):
    TYPE = 1

    def __init__(self, msgid, error, result):
        Message.__init__(self, msgid)
        self.error = error
        self.result = result

    def __str__(self):
        return "<Response id=%s, error=%s, result=%s>" % (self.id, self.error, self.result)

    def to_msg(self):
       msg = [Response.TYPE, self.id, self.error, self.result]
       return json.dumps(msg)

    @staticmethod
    def from_msg(payload):
        data = json.loads(payload)
        assert data[0] == Response.TYPE
        return Response(data[1], data[2], data[3])


class Proto2Command(Command):
    """Compatibility shim that marshals messages from a protocol level
    2 and below remote into ``Command`` objects.
    """

    def __init__(self, name, params):
        Command.__init__(self, None, name, params)

    @staticmethod
    def from_data(data):
        if "emulator_cmd" in data:
            name = "runEmulatorCmd"
        elif "emulator_shell" in data:
            name = "runEmulatorShell"
        else:
            raise ValueError
        return Proto2Command(name, data)


class Proto2Response(Response):
    """Compatibility shim that marshals messages from a protocol level
    2 and below remote into ``Response`` objects.
    """

    def __init__(self, error, result):
        Response.__init__(self, None, error, result)

    @staticmethod
    def from_data(data):
        err, res = None, None
        if "error" in data:
            err = data
        else:
            res = data
        return Proto2Response(err, res)


class TcpTransport(object):
    """Socket client that communciates with Marionette via TCP.

    It speaks the protocol of the remote debugger in Gecko, in which
    messages are always preceded by the message length and a colon, e.g.:

        7:MESSAGE

    On top of this protocol it uses a Marionette message format, that
    depending on the protocol level offered by the remote server, varies.
    Supported protocol levels are 1 and above.
    """
    max_packet_length = 4096
    connection_lost_msg = "Connection to Marionette server is lost. Check gecko.log (desktop firefox) or logcat (b2g) for errors."

    def __init__(self, addr, port, socket_timeout=360.0):
        self.addr = addr
        self.port = port
        self.socket_timeout = socket_timeout

        self.protocol = 1
        self.application_type = None
        self.last_id = 0
        self.expected_responses = []

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(self.socket_timeout)

    def _recv_n_bytes(self, n):
        data = ""
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if chunk == "":
                break
            data += chunk
        return data

    def _unmarshal(self, packet):
        msg = None

        # protocol 3 and above
        if self.protocol >= 3:
            typ = int(packet[1])
            if typ == Command.TYPE:
                msg = Command.from_msg(packet)
            elif typ == Response.TYPE:
                msg = Response.from_msg(packet)

        # protocol 2 and below
        else:
            data = json.loads(packet)

            # emulator callbacks
            if isinstance(data, dict) and any(k in data for k in ("emulator_cmd", "emulator_shell")):
                msg = Proto2Command.from_data(data)

            # everything else
            else:
                msg = Proto2Response.from_data(data)

        return msg

    def receive(self, unmarshal=True):
        """Wait for the next complete response from the remote.

        :param unmarshal: Default is to deserialise the packet and
            return a ``Message`` type.  Setting this to false will return
            the raw packet.
        """
        now = time.time()
        data = ""
        bytes_to_recv = 10

        while time.time() - now < self.socket_timeout:
            try:
                chunk = self.sock.recv(bytes_to_recv)
                data += chunk
            except socket.timeout:
                pass
            else:
                if not chunk:
                    raise IOError(self.connection_lost_msg)

            sep = data.find(":")
            if sep > -1:
                length = data[0:sep]
                remaining = data[sep + 1:]

                if len(remaining) == int(length):
                    if unmarshal:
                        msg = self._unmarshal(remaining)
                        self.last_id = msg.id

                        if isinstance(msg, Response) and self.protocol >= 3:
                            if msg not in self.expected_responses:
                                raise Exception("Received unexpected response: %s" % msg)
                            else:
                                self.expected_responses.remove(msg)

                        return msg
                    else:
                        return remaining

                bytes_to_recv = int(length) - len(remaining)

        raise socket.timeout("connection timed out after %ds" % self.socket_timeout)

    def connect(self):
        """Connect to the server and process the hello message we expect
        to receive in response.

        Returns a tuple of the protocol level and the application type.
        """
        try:
            self.sock.connect((self.addr, self.port))
        except:
            # Unset self.sock so that the next attempt to send will cause
            # another connection attempt.
            self.sock = None
            raise

        self.sock.settimeout(2.0)

        # first packet is always a JSON Object
        # which we can use to tell which protocol level we are at
        raw = self.receive(unmarshal=False)
        hello = json.loads(raw)
        self.protocol = hello.get("marionetteProtocol", 1)
        self.application_type = hello.get("applicationType")

        return (self.protocol, self.application_type)

    def send(self, obj):
        """Send message to the remote server.  Allowed input is a
        ``Message`` instance or a JSON serialisable object.
        """
        if not self.sock:
            self.connect()

        if isinstance(obj, Message):
            data = obj.to_msg()
            self.expected_responses.append(obj)
        else:
            data = json.dumps(obj)
        payload = "%s:%s" % (len(data), data)

        for packet in [payload[i:i + self.max_packet_length] for i in
                       range(0, len(payload), self.max_packet_length)]:
            try:
                self.sock.send(packet)
            except IOError as e:
                if e.errno == errno.EPIPE:
                    raise IOError("%s: %s" % (str(e), self.connection_lost_msg))
                else:
                    raise e

    def respond(self, obj):
        """Send a response to a command.  This can be an arbitrary JSON
        serialisable object or an ``Exception``.
        """
        res, err = None, None
        if isinstance(obj, Exception):
            err = obj
        else:
            res = obj
        msg = Response(self.last_id, err, res)
        self.send(msg)
        return self.receive()

    def request(self, name, params):
        """Sends a message to the remote server and waits for a response
        to come back.
        """
        self.last_id = self.last_id + 1
        cmd = Command(self.last_id, name, params)
        self.send(cmd)
        return self.receive()

    def close(self):
        """Close the socket."""
        if self.sock:
            self.sock.close()

    def __del__(self):
        self.close()
        self.sock = None


def wait_for_port(host, port, timeout=60):
    """ Wait for the specified host/port to be available."""
    starttime = datetime.datetime.now()
    poll_interval = 0.1
    while datetime.datetime.now() - starttime < datetime.timedelta(seconds=timeout):
        sock = None
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((host, port))
            data = sock.recv(16)
            sock.close()
            if ':' in data:
                return True
        except socket.error:
            pass
        finally:
            if sock:
                sock.close()
        time.sleep(poll_interval)
    return False
