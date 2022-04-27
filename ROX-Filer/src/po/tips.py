# Grab the tips from Options.xml

from xml.sax import *
from xml.sax.handler import ContentHandler
import sys, os

sys.stdout.write("Extracting translatable bits from Options.xml...\n")

class Handler(ContentHandler):
	data = ""

	def startElement(self, tag, attrs):
		for x in ['title', 'label', 'end', 'unit']:
			if x in attrs:
				self.trans(attrs[x])
		self.data = ""
	
	def characters(self, data):
		self.data = self.data + data
	
	def endElement(self, tag):
		data = self.data.strip()
		if data:
			self.trans(data)
		self.data = ""
	
	def trans(self, data):
		data = '\\n'.join(data.split('\n'))
		if data:
			out.write('_("%s")\n' % data.replace('"', '\\"'))

try:
	os.chdir("po")
except OSError:
	pass
	
out = open('../tips', 'w')
parse('../../Options.xml', Handler())
out.close()
