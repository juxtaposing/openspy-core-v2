from cgi import parse_qs, escape
import xml.etree.ElementTree as ET

import binascii
from M2Crypto import RSA, BIO
import md5, struct, os

from collections import OrderedDict
import jwt

from BaseService import BaseService
import httplib, urllib

class GS_AuthService(BaseService):
    LOGIN_SERVER = 'localhost:80'
    LOGIN_SCRIPT = '/backend/auth'
    SECRET_AUTH_KEY = "dGhpc2lzdGhla2V5dGhpc2lzdGhla2V5dGhpc2lzdGhla2V5"

    LOGIN_RESPONSE_SUCCESS = 0
    LOGIN_RESPONSE_SERVERINITFAILED = 1
    LOGIN_RESPONSE_USER_NOT_FOUND = 2
    LOGIN_RESPONSE_INVALID_PASSWORD = 3
    LOGIN_RESPONSE_INVALID_PROFILE = 4
    LOGIN_RESPONSE_UNIQUE_NICK_EXPIRED = 5
    LOGIN_RESPONSE_DB_ERROR = 6
    LOGIN_RESPONSE_SERVER_ERROR = 7


    def generate_signature(self, rsa, length, version, auth_user_dir, peerkey, server_data, use_md5):
        if use_md5:
            validity_sig = md5.new()
            validity_sig.update(struct.pack("I", length))
            validity_sig.update(struct.pack("I", version))
            validity_sig.update(struct.pack("I", int(auth_user_dir['partnercode'])))
            validity_sig.update(struct.pack("I", int(auth_user_dir['namespaceid'])))
            validity_sig.update(struct.pack("I", int(auth_user_dir['userid'])))
            validity_sig.update(struct.pack("I", int(auth_user_dir['profileid'])))
            validity_sig.update(struct.pack("I", int(auth_user_dir['expiretime'])))
            validity_sig.update(auth_user_dir['profilenick'])
            validity_sig.update(auth_user_dir['uniquenick'])
            validity_sig.update(auth_user_dir['cdkeyhash'])
            validity_sig.update(binascii.unhexlify(peerkey['modulus']))
            validity_sig.update(binascii.unhexlify(peerkey['exponent']))
            validity_sig.update(server_data)
            md5_data = validity_sig.digest()
            return binascii.hexlify(rsa.sign(md5_data, 'md5')).upper()    
        return None


    #will be used if LOGIN_RESPONE_* differs from backend auth
    def convert_reason_code(self, reason):
        return str(reason)

    def try_authenticate(self, login_data):
        login_data["user_login"] = False
        login_data["profile_login"] = False
        params = jwt.encode(login_data, self.SECRET_AUTH_KEY, algorithm='HS256')
        #params = urllib.urlencode(params)
        
        headers = {"Content-type": "application/x-www-form-urlencoded","Accept": "text/plain"}
        conn = httplib.HTTPConnection(self.LOGIN_SERVER)

        conn.request("POST", self.LOGIN_SCRIPT, params, headers)
        response = conn.getresponse().read()
        #print("Resp: {}\n".format(response))
        response = jwt.decode(response, self.SECRET_AUTH_KEY, algorithm='HS256')

        

        

        if "success" in response and response["success"] == True:
            profile = response['profile']
            response['profile'] = OrderedDict()
            #stupid copy for XML reader of web services C SDK(bug where reading out of order = invalid cert)
            response['profile']['partnercode'] = profile['user']['partnercode']
            response['profile']['namespaceid'] = profile['namespaceid']
            response['profile']['userid'] = profile['user']['id']
            response['profile']['profileid'] = profile['id']
            response['profile']['expiretime'] = response['expiretime']
            response['profile']['profilenick'] = profile['nick']
            response['profile']['uniquenick'] = profile['uniquenick']
            response['profile']['cdkeyhash'] = '9f86d081884c7d659a2feaa0c55ad015'.upper()
            return response

        #failed, delete profile if exists and send error
        if 'profile' in response:
            del response['profile']

        return response


    def handle_remoteauth_login(self, xml_tree, rsa):
        resp_xml = ET.Element('SOAP-ENV:Envelope')
        body = ET.SubElement(resp_xml, 'SOAP-ENV:Body')
        login_result = ET.SubElement(body, 'ns1:LoginUniqueNickResult')

        response_code_node = ET.SubElement(login_result, 'ns1:responseCode')
        response_code_node.text = str(self.LOGIN_RESPONSE_SERVERINITFAILED)
        return resp_xml

    def handle_profile_login(self, xml_tree, rsa):
        resp_xml = ET.Element('SOAP-ENV:Envelope')
        body = ET.SubElement(resp_xml, 'SOAP-ENV:Body')
        login_result = ET.SubElement(body, 'ns1:LoginProfileResult')

        response_code_node = ET.SubElement(login_result, 'ns1:responseCode')
        


        #auth stuff

        certificate_node = ET.SubElement(login_result, 'ns1:certificate')

        peerkeyprivate_node = ET.SubElement(login_result, 'ns1:peerkeyprivate')
        peerkeyprivate_node.text = '0'

        length_node = ET.SubElement(certificate_node, 'ns1:length') #???
        length_node.text = '111'

        version_node = ET.SubElement(certificate_node, 'ns1:version')
        version_node.text = '1'

        #get login info
        profilenick_node = xml_tree.find('{http://gamespy.net/AuthService/}profilenick')
        profilenick = profilenick_node.text

        email_node = xml_tree.find('{http://gamespy.net/AuthService/}email')
        email = email_node.text

        partnercode_node = xml_tree.find('{http://gamespy.net/AuthService/}partnercode')
        partnercode = partnercode_node.text

        namespaceid_node = xml_tree.find('{http://gamespy.net/AuthService/}partnercode')
        namespaceid = namespaceid_node.text

        #decrypt pw
        encrypted_pass = xml_tree.find('{http://gamespy.net/AuthService/}password').find('{http://gamespy.net/AuthService/}Value')
        password = rsa.private_decrypt(binascii.unhexlify(encrypted_pass.text), RSA.pkcs1_padding) 

        #try auth
        auth_user_dir = self.try_authenticate({'email': email, 'profilenick': profilenick, 'password': password, 'namespaceid': int(namespaceid), 'partnercode': int(partnercode), 'hash_type': 'plain'})
        if 'profile' in auth_user_dir:
            response_code_node.text = str(self.LOGIN_RESPONSE_SUCCESS)
            #populate user info
            for k,v in auth_user_dir['profile'].items():
                node = ET.SubElement(certificate_node, 'ns1:{}'.format(k))
                node.text = str(v)
        else: #send error data
            response_code_node.text = self.convert_reason_code(auth_user_dir['reason'])


        #encrypted server data
        peerkeymodulus_node = ET.SubElement(certificate_node, 'ns1:peerkeymodulus')
        rsa_modulus = binascii.hexlify(rsa.n)
        peerkeymodulus_node.text = rsa_modulus[-128:].upper()

        peerkeyexponent_node = ET.SubElement(certificate_node, 'ns1:peerkeyexponent')
        rsa_exponent = binascii.hexlify(rsa.e)
        peerkeyexponent_node.text = rsa_exponent[-6:]

        serverdata_node = ET.SubElement(certificate_node, 'ns1:serverdata')

        server_data = os.urandom(128)
        serverdata_node.text = binascii.hexlify(server_data)

        signature_node = ET.SubElement(certificate_node, 'ns1:signature')

        peerkey = {}
        peerkey['exponent'] = peerkeyexponent_node.text
        peerkey['modulus'] = peerkeymodulus_node.text
        signature_node.text = self.generate_signature(rsa, int(length_node.text), int(version_node.text), auth_user_dir['profile'], peerkey, server_data, True)


        return resp_xml


    def handle_unique_login(self, xml_tree, rsa):
        resp_xml = ET.Element('SOAP-ENV:Envelope')
        body = ET.SubElement(resp_xml, 'SOAP-ENV:Body')
        login_result = ET.SubElement(body, 'ns1:LoginUniqueNickResult')

        response_code_node = ET.SubElement(login_result, 'ns1:responseCode')


        #auth stuff

        certificate_node = ET.SubElement(login_result, 'ns1:certificate')

        peerkeyprivate_node = ET.SubElement(login_result, 'ns1:peerkeyprivate')
        peerkeyprivate_node.text = '0'

        length_node = ET.SubElement(certificate_node, 'ns1:length') #???
        length_node.text = '111'

        version_node = ET.SubElement(certificate_node, 'ns1:version')
        version_node.text = '1'

        
        #get request info
        uniquenick_node = xml_tree.find('{http://gamespy.net/AuthService/}uniquenick')
        uniquenick = uniquenick_node.text

        partnercode_node = xml_tree.find('{http://gamespy.net/AuthService/}partnercode')
        partnercode = partnercode_node.text

        namespaceid_node = xml_tree.find('{http://gamespy.net/AuthService/}partnercode')
        namespaceid = namespaceid_node.text

        #decrypt pw
        encrypted_pass = xml_tree.find('{http://gamespy.net/AuthService/}password').find('{http://gamespy.net/AuthService/}Value')
        password = rsa.private_decrypt(binascii.unhexlify(encrypted_pass.text), RSA.pkcs1_padding) 

        #try auth
        auth_user_dir = self.try_authenticate({'uniquenick': uniquenick, 'password': password, 'namespaceid': int(namespaceid), 'partnercode': int(partnercode), 'hash_type': 'plain'})

        if 'profile' in auth_user_dir:
            response_code_node.text = str(self.LOGIN_RESPONSE_SUCCESS)
            #populate user info
            for k,v in auth_user_dir['profile'].items():
                node = ET.SubElement(certificate_node, 'ns1:{}'.format(k))
                node.text = str(v)
        else: #send error data
            response_code_node.text = convert_reason_code(auth_user_dir['reason'])

        #encrypted server data
        peerkeymodulus_node = ET.SubElement(certificate_node, 'ns1:peerkeymodulus')
        rsa_modulus = binascii.hexlify(rsa.n)
        peerkeymodulus_node.text = rsa_modulus[-128:].upper()

        peerkeyexponent_node = ET.SubElement(certificate_node, 'ns1:peerkeyexponent')
        rsa_exponent = binascii.hexlify(rsa.e)
        peerkeyexponent_node.text = rsa_exponent[-6:]

        serverdata_node = ET.SubElement(certificate_node, 'ns1:serverdata')

        server_data = os.urandom(128)
        serverdata_node.text = binascii.hexlify(server_data)

        signature_node = ET.SubElement(certificate_node, 'ns1:signature')

        peerkey = {}
        peerkey['exponent'] = peerkeyexponent_node.text
        peerkey['modulus'] = peerkeymodulus_node.text
        signature_node.text = self.generate_signature(rsa, int(length_node.text), int(version_node.text), auth_user_dir['profile'], peerkey, server_data, True)

        return resp_xml


    def get_pass(*args):
            return str('123321')

    def run(self, env, start_response):
        # the environment variable CONTENT_LENGTH may be empty or missing
        try:
            request_body_size = int(env.get('CONTENT_LENGTH', 0))
        except (ValueError):
            request_body_size = 0

        # When the method is POST the variable will be sent
        # in the HTTP request body which is passed by the WSGI server
        # in the file like wsgi.input environment variable.
        request_body = env['wsgi.input'].read(request_body_size)
       # d = parse_qs(request_body)

        start_response('200 OK', [('Content-Type','text/html')])

        private_key_file = open("openspy_webservices_private.pem","r")

        bio = BIO.MemoryBuffer(private_key_file.read())
        
        rsa = RSA.load_key_bio(bio, self.get_pass)
        
        tree = ET.ElementTree(ET.fromstring(request_body))

        login_profile_tree = tree.find('.//{http://gamespy.net/AuthService/}LoginProfile')
        login_remoteauth_tree = tree.find('.//{http://gamespy.net/AuthService/}LoginRemoteAuth')
        login_uniquenick_tree = tree.find('.//{http://gamespy.net/AuthService/}LoginUniqueNick')
        login_ps3_tree = tree.find('.//{http://gamespy.net/AuthService/}LoginPs3Cert')
        login_facebook_tree = tree.find('.//{http://gamespy.net/AuthService/}LoginFacebook')

        resp = None
        if login_uniquenick_tree != None:
            resp = self.handle_unique_login(login_uniquenick_tree, rsa)
        elif login_profile_tree != None:
            resp = self.handle_profile_login(login_profile_tree, rsa)
        elif login_remoteauth_tree != None:
            resp = self.handle_remoteauth_login(login_remoteauth_tree, rsa)

        if resp != None:
            return ET.tostring(resp, encoding='utf8', method='xml')
        else:
            return 'Fatal error'
