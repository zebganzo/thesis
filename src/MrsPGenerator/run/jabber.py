#!/usr/bin/env python
import os
import sys
import xmpp

class Jabber(object):
    def __init__(self, target):
        self.target = target
        self.client = self.__create_client()

    def __create_client(self):
        params = {}
        conf_file = os.environ['HOME'] + '/.xsend'

        if os.access(conf_file, os.R_OK):
            for line in open(conf_file, 'r').readlines():
                key, val = line.strip().split('=', 1)
                params[key.lower()] = val

        for field in ['login', 'password']:
            if field not in params.keys():
                with open(conf_file, 'wc') as f:
                    f.write('#LOGIN=romeo@montague.net\n#PASSWORD=juliet\n')
                raise IOError("Please ensure the ~/.xsend file has valid "+\
                              "credentials for sending messages.")

        jid = xmpp.protocol.JID(params['login'])
        client = xmpp.Client(jid.getDomain(), debug=[])

        client.connect(server=(jid.getDomain(), 5223))
        client.auth(jid.getNode(), params['password'])

        sys.stderr.write(("Jabber client loaded. Add '%s' to your friends " + \
                          "or Jabber messages will not send") % params['login'])

        return client

    def send(self, text):
        message = xmpp.protocol.Message(to=self.target, body=text, typ='chat')
        self.client.send(message)
