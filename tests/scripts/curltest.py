#!/usr/bin/python
import pycurl

c = pycurl.Curl()
c.setopt(c.URL, 'localhost:4242/obix')
c.perform()

