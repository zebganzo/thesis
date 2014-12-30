import os
import pwd
import smtplib
import socket

class Emailer(object):
    def __init__(self, target):
        user = pwd.getpwuid(os.getuid())[0]
        host = socket.gethostname()

        self.sender = "%s@%s" % (user, host)
        self.target = target

        self.body = "\r\n".join(["From: %s" % self.sender,
                                 "To: %s" % target,
                                 "Subject: Test Completed!", "", "{}"])

        self.mail = smtplib.SMTP("localhost")

        # Hopefully crash if the server is not running
        self.mail.ehlo()

    def close(self):
        self.mail.quit()

    def send(self, text):
        self.mail.sendmail(self.sender, [self.target],
                           self.body.format(text))
