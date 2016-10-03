/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

MARIONETTE_TIMEOUT = 60000;
MARIONETTE_HEAD_JS = 'head.js';

const SCAN_RETRY_CNT = 5;

const EAP_USERNAME = 'username';
const EAP_PASSWORD = 'password';

const SERVER_EAP_USER_CONF = {
  path: HOSTAPD_CONFIG_PATH + 'hostapd.eap_user',
  content: '* PEAP,TTLS,TLS\n' +
    '"' + EAP_USERNAME + '" MSCHAPV2,TTLS-MSCHAPV2 "' + EAP_PASSWORD + '" [2]\n'
};
const CA_CERT = {
  path: HOSTAPD_CONFIG_PATH + 'ca.pem',
  content: '-----BEGIN CERTIFICATE-----\n' +
    'MIIDsTCCApmgAwIBAgIJAKxTf+8X8qngMA0GCSqGSIb3DQEBCwUAMG4xCzAJBgNV\n' +
    'BAYTAlRXMRMwEQYDVQQIDApTb21lLVN0YXRlMREwDwYDVQQKDAhjaHVja2xlZTER\n' +
    'MA8GA1UEAwwIY2h1Y2tsZWUxJDAiBgkqhkiG9w0BCQEWFWNodWNrbGkwNzA2QGdt\n' +
    'YWlsLmNvbTAgFw0xNDEyMjQxMTI4NTBaGA8yMjg4MTAwNzExMjg1MFowbjELMAkG\n' +
    'A1UEBhMCVFcxEzARBgNVBAgMClNvbWUtU3RhdGUxETAPBgNVBAoMCGNodWNrbGVl\n' +
    'MREwDwYDVQQDDAhjaHVja2xlZTEkMCIGCSqGSIb3DQEJARYVY2h1Y2tsaTA3MDZA\n' +
    'Z21haWwuY29tMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAo3c2yFxY\n' +
    'o6gGg0I83jy00ME+MAfzCd+4ShL45ZLqysQP93jRBfPzU9ZuZ29ysVwgWIdqkZao\n' +
    'XTuV/NAW2GMGd8W1jQJ3J81fjb9wvhlny3rrACwvUn1N1S1BnM+BAAiDLGxEmvAQ\n' +
    'onp2aaa6HsHsYS8ONX+d2Qh4LEA4vupeSGAqJychCZv/l+aq/ErFZhFYB3CPUQEt\n' +
    'cClO24ucsIYP95lA0zhscnmAj06qplFD4Bv6IVrdDqujy1zNwCQwsJq/8OQdaTN/\n' +
    'h3y9pWvNKMBMM2niOUAjtuNpqsSK/lTS1WAT3PdtVECX9fYBi0Bg+HM92xs/6gt6\n' +
    'kh9jPV8keXHvSwIDAQABo1AwTjAdBgNVHQ4EFgQU7hBqhuG04xeCzrQ3ngx18ZJ3\n' +
    'lUswHwYDVR0jBBgwFoAU7hBqhuG04xeCzrQ3ngx18ZJ3lUswDAYDVR0TBAUwAwEB\n' +
    '/zANBgkqhkiG9w0BAQsFAAOCAQEAFYX2iy680GAnBTttk0gyX6gk+8pYr3D22k/G\n' +
    '6rvcjefzS7ELQPRKr6mfmwXq3mMf/4jiS2zI5zmXsestPYzHYxf2viQ6t7vr9XiJ\n' +
    '3WfFjNw4ERlRisAvg0aqqTNNQq5v2VME4sdFZagy217f73C7azwCHl0bqOLH05rl\n' +
    '8RubOxiHEj7ZybJqnRciK/bht4D+rZkwf4bBBmoloqH7xT0+rFQclpYXDGGjNUQB\n' +
    'LcHLF10xcr7g3ZVVu82fe6+d85gIGOIMR9+TKhdw6gO3CNcnDAj6gxksghgtcxmh\n' +
    'OzOggCn7nlIwImtsg2sZkpWB4lEi9hdv4lkNuyFjOL3bnuc+NA==\n' +
    '-----END CERTIFICATE-----\n'
};

const SERVER_CERT = {
  path: HOSTAPD_CONFIG_PATH + 'server.pem',
  content: '-----BEGIN CERTIFICATE-----\n' +
    'MIID1DCCArygAwIBAgIBADANBgkqhkiG9w0BAQsFADBuMQswCQYDVQQGEwJUVzET\n' +
    'MBEGA1UECAwKU29tZS1TdGF0ZTERMA8GA1UECgwIY2h1Y2tsZWUxETAPBgNVBAMM\n' +
    'CGNodWNrbGVlMSQwIgYJKoZIhvcNAQkBFhVjaHVja2xpMDcwNkBnbWFpbC5jb20w\n' +
    'IBcNMTQxMjI0MTEyOTQ5WhgPMjI4ODEwMDcxMTI5NDlaMG4xCzAJBgNVBAYTAlRX\n' +
    'MRMwEQYDVQQIDApTb21lLVN0YXRlMREwDwYDVQQKDAhjaHVja2xlZTERMA8GA1UE\n' +
    'AwwIY2h1Y2tsZWUxJDAiBgkqhkiG9w0BCQEWFWNodWNrbGkwNzA2QGdtYWlsLmNv\n' +
    'bTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMdhQmKilTJbWZRxTiSV\n' +
    'rqIU+LYW1RKghx5o+0JpNRJVLuz5kBMaNskbbfUSNuHbEq0QA9BDKAZWIc4LSotk\n' +
    'lCo8TbcO9CJvJPQGGjGdHcohWX5vy6BE/OVE46CUteMFyZF6F8R2fNUww08iR/u1\n' +
    'YZebL5pWO1j43sPpAzEy6Tij2ACPt6EZcFaZG3SF2mVJWkCQnBqrojP65WUvZQqp\n' +
    'seUhW2YAS8Nu0Yrohgxz6VYk+cNDuDZVGs6qWRStZzJfYrfc76DtkHof5B14M+xp\n' +
    'XJaBLxN+whvnYkDTfinaCxnW1O7eXUltr87fLc5zmeBkgwaiaQuIdcfZm7vDUiz8\n' +
    'vnUCAwEAAaN7MHkwCQYDVR0TBAIwADAsBglghkgBhvhCAQ0EHxYdT3BlblNTTCBH\n' +
    'ZW5lcmF0ZWQgQ2VydGlmaWNhdGUwHQYDVR0OBBYEFKK4f9/YavTHOfEiAB83Deac\n' +
    '6gT5MB8GA1UdIwQYMBaAFO4QaobhtOMXgs60N54MdfGSd5VLMA0GCSqGSIb3DQEB\n' +
    'CwUAA4IBAQBWnO9o9KSJIqjoz5Nwll63ULOdcvgGdOeJIw1fcKQ817Rsp+TVcjcH\n' +
    'IrIADsT/QZGXRO/l6p1750e2iFtJEo1hsRaxtA1wWn2I9HO3+av2spQhr3jpYGPf\n' +
    'zpsMTp4RNYV7Q8+q1kZIz9PY4V1T0p6lveK8+fUj2hSLnxSj0QiGSJJtnEC3w4Rv\n' +
    'C9T6oUwIeToULmi+8FXQFdEqwKRU98DPq3eLzN28ZxUgoPE1C8+42D2UW8uyp/Gm\n' +
    'tGOa/k7nzkCdVqZI7lX7f0AjEvQgjtAMQ/k7Mhxx7TzW2HO+1YPMoKji6Z4WkNwt\n' +
    'JEj9ZUBSNt8B26UksJMBDkcvSegF3a7o\n' +
    '-----END CERTIFICATE-----\n'
};

const SERVER_KEY = {
  path: HOSTAPD_CONFIG_PATH + 'server.key',
  content: '-----BEGIN RSA PRIVATE KEY-----\n' +
    'MIIEpAIBAAKCAQEAx2FCYqKVMltZlHFOJJWuohT4thbVEqCHHmj7Qmk1ElUu7PmQ\n' +
    'Exo2yRtt9RI24dsSrRAD0EMoBlYhzgtKi2SUKjxNtw70Im8k9AYaMZ0dyiFZfm/L\n' +
    'oET85UTjoJS14wXJkXoXxHZ81TDDTyJH+7Vhl5svmlY7WPjew+kDMTLpOKPYAI+3\n' +
    'oRlwVpkbdIXaZUlaQJCcGquiM/rlZS9lCqmx5SFbZgBLw27RiuiGDHPpViT5w0O4\n' +
    'NlUazqpZFK1nMl9it9zvoO2Qeh/kHXgz7GlcloEvE37CG+diQNN+KdoLGdbU7t5d\n' +
    'SW2vzt8tznOZ4GSDBqJpC4h1x9mbu8NSLPy+dQIDAQABAoIBAASG4Mr8hgaurEoC\n' +
    'iJOsElr7vunjetMBcg/uskW/vcS8ymP3Bp5oafYG+WgnEbfvEW18f5mq7K24JuxW\n' +
    'tUqU7ghHdjxByqk9fMlNmiqmNpbwSufkAeuRpWxPNBvhRH/zEbCL5R5A0nTEtqqF\n' +
    'TL0aUSzwCRSoAJD0lZo9ICVt0n3GsDyM9rqQg/uZmh1qsRdwPsRuYORND9g48rKq\n' +
    '6WN9leskSxhhsYE2D9ocOFd9bNt8Zxejh9ppVSnG/KsIdt18iBzcabatgAQ046fb\n' +
    'Z3vprcZJLg93Sg2gSuVqlSTs3M2W8VQnm22/EBMb1y0M48MSRCgnbPLG/CcCLLfF\n' +
    'LwxCOgECgYEA/eYt67xyJ6JeAdxdwOZuT1WWGbFpLiG9+2OgiHumyRQ5969XMTWo\n' +
    'fIhMKchDdjoy9RR236\/\/EFCs7UEyB7+a7ODRzNiK2zCD8Smjp+21fUPSthEeQesk\n' +
    'eiMYICIu5Ay35x9sxIX+XOUVvRhPOGcD29GVeRnKh1inTHOz2dje8LkCgYEAyQeY\n' +
    'STi9jjCEcHkM1E/UeDiLfHHepLXi8wS41JNRHl5Jacp7XB5djAjKu/jf367/VpFy\n' +
    'GDDMetE7n8eWkrnAvovxOwZ000YDMtL1sUYSjL+XtBS5s6VY1p4qaSAY9nUUGrJh\n' +
    'JvtvsuI7SKTtL+60vjBOH7zDnvOdBgAp0utLhZ0CgYEAuLzzqrPKB8afShFSchn4\n' +
    'J2dpuLYahsNsXW7HDqeR2nsKFosRETAusLXnXPtnAq4kB6jlOarwFqnsuRCX24Vx\n' +
    'r2uBm9/vYL7zMdUPTA+s30ErHuhjsKjsOKYyVqcooSwT32pBFNk+E89nutfmRG7I\n' +
    'IvhjHuNCNqqtx/Xj5d1jkZkCgYBQicppC2Jl5OoqZVTOem0U/RJk+PnJ41TZJ7sk\n' +
    '7yBAmmWvDH\/\/l+rCf4M5a6vFYcbKV9rt9h711X2dtciNX/3oWQh8LUoAmrwNUJc+\n' +
    'PmSQHvIYI3WCk2vUD+nN1B4sHxu+1lg11eYaNKiroeeknG2tBI1ICcgVlmQCU25u\n' +
    'IfZPwQKBgQCdO6QHhPLtcHUDNFA6FQ1jKL1iEd7G0JLVRz4Xkpkn1Vrr5MD6JFDa\n' +
    '5ccabADyl0lpFqDIVJQIzLku2hOD2i9aBNCY0pL391HeOS7CkZX+TdOY1tquoBq5\n' +
    'MnmixZjDCVd2VcrVyTA6ntOBoharKFW0rH1PqU+qu7dZF7CBPbAdEw==\n' +
    '-----END RSA PRIVATE KEY-----\n'
};

const WPA_EAP_AP_LIST = [
  {
    ssid: 'WPA-EAP-PEAP',
    ieee8021x: 1,
    eapol_version: 1,
    eap_server: 1,
    eapol_key_index_workaround: 0,
    eap_user_file: SERVER_EAP_USER_CONF.path,
    ca_cert: CA_CERT.path,
    server_cert: SERVER_CERT.path,
    private_key: SERVER_KEY.path,
    wpa: 3,
    wpa_key_mgmt: 'WPA-EAP'
  }
];

const CLIENT_PKCS12_CERT = {
  nickname: 'client',
  password: 'password',
  usage: ['UserCert', 'ServerCert'],
  content: [0x30, 0x82, 0x0E, 0x01, 0x02, 0x01, 0x03, 0x30,
    0x82, 0x0D, 0xC7, 0x06, 0x09, 0x2A, 0x86, 0x48,
    0x86, 0xF7, 0x0D, 0x01, 0x07, 0x01, 0xA0, 0x82,
    0x0D, 0xB8, 0x04, 0x82, 0x0D, 0xB4, 0x30, 0x82,
    0x0D, 0xB0, 0x30, 0x82, 0x08, 0x67, 0x06, 0x09,
    0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x07,
    0x06, 0xA0, 0x82, 0x08, 0x58, 0x30, 0x82, 0x08,
    0x54, 0x02, 0x01, 0x00, 0x30, 0x82, 0x08, 0x4D,
    0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D,
    0x01, 0x07, 0x01, 0x30, 0x1C, 0x06, 0x0A, 0x2A,
    0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x0C, 0x01,
    0x06, 0x30, 0x0E, 0x04, 0x08, 0x67, 0x7A, 0xF3,
    0x61, 0xBE, 0xE0, 0x51, 0xC1, 0x02, 0x02, 0x08,
    0x00, 0x80, 0x82, 0x08, 0x20, 0xFC, 0x6A, 0x79,
    0xA1, 0x6C, 0xAF, 0xBE, 0xEE, 0x62, 0x45, 0x33,
    0xB8, 0x48, 0xE1, 0x68, 0xA1, 0x15, 0x11, 0x4B,
    0x95, 0xCB, 0x77, 0xC0, 0x5D, 0xA2, 0xCB, 0xDB,
    0xD1, 0x83, 0x74, 0x60, 0xD7, 0xEC, 0x42, 0xA6,
    0x3A, 0x23, 0xF7, 0x85, 0xEB, 0xC1, 0xFE, 0x6A,
    0x57, 0x8E, 0xC1, 0x44, 0xF3, 0x1F, 0xFE, 0xB8,
    0x2D, 0x8C, 0x4D, 0xC9, 0x5B, 0xAE, 0x21, 0x2E,
    0x4C, 0x1A, 0xEB, 0x84, 0x09, 0xF3, 0x40, 0x92,
    0x39, 0x7F, 0x56, 0x02, 0x46, 0x61, 0x16, 0xDE,
    0x5C, 0x48, 0xB6, 0x0C, 0x1D, 0xD3, 0x5F, 0x10,
    0x9A, 0x39, 0xB8, 0x66, 0x31, 0xFC, 0x39, 0x71,
    0x87, 0x23, 0x46, 0x9D, 0xE8, 0x3C, 0x2B, 0xA1,
    0x39, 0x8A, 0xD3, 0xFF, 0xD9, 0x43, 0xB6, 0x61,
    0xC6, 0x67, 0x70, 0x40, 0xBD, 0xFE, 0xD3, 0xC1,
    0x68, 0xF5, 0xF7, 0xC8, 0x89, 0xD8, 0x17, 0xC5,
    0xE8, 0x3D, 0x29, 0xD5, 0x91, 0xDF, 0x1F, 0x56,
    0x74, 0x5A, 0xC4, 0xA8, 0x14, 0xBA, 0xD4, 0xFA,
    0x13, 0x49, 0x2A, 0x9F, 0x63, 0xF1, 0xB2, 0x45,
    0xF1, 0xF0, 0x2A, 0xDD, 0x75, 0x66, 0x8A, 0xF7,
    0xAB, 0x73, 0x86, 0x26, 0x9D, 0x1F, 0x07, 0xAD,
    0xD3, 0xFE, 0xE0, 0xA3, 0xED, 0xA0, 0x96, 0x3E,
    0x1E, 0x89, 0x86, 0x02, 0x4C, 0x28, 0xFD, 0x57,
    0xA1, 0x67, 0x55, 0xF0, 0x82, 0x3B, 0x7F, 0xCC,
    0x2A, 0x32, 0x01, 0x93, 0x1D, 0x8B, 0x66, 0x8A,
    0x20, 0x52, 0x84, 0xDD, 0x2C, 0xFD, 0xEE, 0x72,
    0xF3, 0x8C, 0x58, 0xB9, 0x99, 0xE5, 0xC1, 0x22,
    0x63, 0x59, 0x00, 0xE2, 0x76, 0xC5, 0x3A, 0x17,
    0x7F, 0x93, 0xE9, 0x67, 0x61, 0xAA, 0x10, 0xC3,
    0xD9, 0xC8, 0x24, 0x46, 0x5B, 0xBE, 0x8C, 0x1F,
    0x2D, 0x66, 0x48, 0xD2, 0x02, 0x11, 0xFB, 0x74,
    0x14, 0x76, 0x76, 0x5A, 0x98, 0x54, 0x35, 0xA7,
    0x85, 0x66, 0x20, 0x26, 0x8B, 0x13, 0x6F, 0x68,
    0xE3, 0xC9, 0x58, 0x7D, 0x1C, 0x3E, 0x01, 0x8D,
    0xF8, 0xD6, 0x7F, 0xCF, 0xA2, 0x07, 0xB7, 0x95,
    0xFD, 0xF0, 0x02, 0x34, 0x32, 0x30, 0xE8, 0xD4,
    0x57, 0x5E, 0x53, 0xFB, 0x54, 0xE2, 0x03, 0x32,
    0xCC, 0x52, 0x2E, 0xD2, 0x35, 0xD9, 0x58, 0x85,
    0x2D, 0xEC, 0x2D, 0x71, 0xD1, 0x8A, 0x29, 0xD0,
    0xB0, 0x24, 0xBD, 0x24, 0xDC, 0x1A, 0x28, 0x3F,
    0xA0, 0x12, 0x81, 0x15, 0x24, 0xC9, 0xB5, 0x4A,
    0x23, 0xB6, 0xA3, 0x45, 0x50, 0x2D, 0x73, 0x99,
    0x6B, 0x1C, 0xFB, 0xA4, 0x53, 0xD7, 0x5C, 0xF4,
    0x6C, 0xB0, 0xE5, 0x74, 0xB3, 0x76, 0xF8, 0xB1,
    0x0D, 0x59, 0x70, 0x9F, 0xCA, 0xDE, 0xF2, 0xAA,
    0x4C, 0x7D, 0x11, 0x54, 0xC4, 0x19, 0x0F, 0x36,
    0x4A, 0x62, 0xFF, 0x8B, 0x10, 0xCB, 0x93, 0x50,
    0xDA, 0x79, 0x5E, 0x4E, 0x09, 0x1F, 0x22, 0xC8,
    0x19, 0x85, 0xE9, 0xEE, 0xB7, 0x71, 0x65, 0xB9,
    0x10, 0xD2, 0x0A, 0x73, 0x5B, 0xA6, 0xDA, 0x37,
    0x46, 0x02, 0x00, 0x98, 0x9E, 0x20, 0x6C, 0x7D,
    0xC7, 0x69, 0xBB, 0xC2, 0x00, 0x40, 0x9C, 0x57,
    0x00, 0xC2, 0x36, 0x76, 0xE8, 0x2A, 0x8D, 0xAD,
    0x62, 0x57, 0xC8, 0xD0, 0x9D, 0x66, 0x27, 0x5A,
    0xD8, 0x0D, 0x35, 0x60, 0x28, 0x38, 0x62, 0x94,
    0x78, 0x36, 0x25, 0x58, 0xFD, 0xF8, 0x66, 0x1F,
    0x68, 0x04, 0x0F, 0xD8, 0x00, 0xDF, 0xA0, 0x6C,
    0x25, 0x42, 0x9A, 0x4C, 0xEB, 0x80, 0x13, 0x51,
    0x7D, 0x2D, 0xA8, 0x89, 0xD6, 0x1B, 0x67, 0x72,
    0x01, 0xF3, 0x2D, 0x16, 0x77, 0xFE, 0x22, 0xBC,
    0x8A, 0x45, 0x09, 0x1F, 0x9C, 0x2F, 0x2A, 0xA9,
    0x61, 0x5B, 0x4A, 0xE6, 0x64, 0x2C, 0x62, 0x1A,
    0x3A, 0x96, 0xE6, 0x0A, 0xAE, 0x05, 0x1A, 0xC8,
    0xCB, 0xD6, 0x8F, 0x3A, 0x4B, 0xE0, 0x7F, 0x82,
    0xB4, 0x98, 0xF1, 0x9D, 0xD7, 0x14, 0x76, 0x5E,
    0x77, 0x85, 0x87, 0xEC, 0x13, 0xDA, 0xFD, 0xAF,
    0xCB, 0xA3, 0x1C, 0x99, 0xC1, 0xFE, 0x17, 0x0C,
    0x40, 0x4D, 0x3C, 0x8F, 0x70, 0x86, 0x63, 0x64,
    0xB7, 0x75, 0xA8, 0x71, 0x36, 0xDC, 0x54, 0x10,
    0x57, 0x0C, 0xA8, 0xF2, 0xA1, 0xBB, 0xED, 0x03,
    0x41, 0x57, 0x34, 0x2C, 0x8F, 0x7C, 0xA0, 0x09,
    0xF3, 0x9E, 0x41, 0xB7, 0xA8, 0xD4, 0x66, 0x0D,
    0x0D, 0xC0, 0x6A, 0xFC, 0x6A, 0xA2, 0xAC, 0xE2,
    0x60, 0x00, 0xE3, 0xF7, 0x75, 0x43, 0x23, 0xEB,
    0xC8, 0x61, 0xFA, 0xB3, 0xB8, 0x28, 0xCE, 0xCA,
    0xF4, 0x47, 0x7F, 0x30, 0x6D, 0x61, 0x89, 0x47,
    0xA1, 0x4A, 0xFE, 0xD1, 0x21, 0x0B, 0x6D, 0xF4,
    0x3F, 0x00, 0x86, 0x30, 0x8E, 0x33, 0x21, 0x6F,
    0xDA, 0x15, 0xFD, 0x5F, 0xEC, 0x8E, 0xF1, 0x12,
    0x3F, 0xC9, 0x83, 0x0C, 0xCA, 0x22, 0x01, 0xF1,
    0x70, 0x5F, 0x1F, 0x66, 0xB5, 0xF8, 0x3E, 0x7A,
    0x6F, 0xDE, 0xDB, 0xA7, 0x8D, 0x18, 0x9E, 0xBE,
    0xDB, 0xAD, 0x3D, 0x66, 0x30, 0xC1, 0x6C, 0x0C,
    0x87, 0xB4, 0x65, 0x75, 0xE0, 0x9D, 0xEA, 0x16,
    0x0D, 0x07, 0x37, 0x33, 0xC5, 0xEC, 0x97, 0x93,
    0x37, 0xEB, 0x8E, 0x65, 0x9C, 0x40, 0x63, 0x6C,
    0x43, 0x60, 0xB0, 0x40, 0x4D, 0x85, 0xEF, 0xC2,
    0x47, 0x5F, 0xE7, 0x6B, 0xCB, 0x40, 0xE8, 0xEA,
    0xD8, 0xAB, 0xB1, 0x9A, 0x72, 0xDC, 0x4C, 0x14,
    0xFA, 0x43, 0x61, 0x5F, 0xA6, 0x5C, 0x3A, 0x05,
    0x17, 0x2E, 0x74, 0xF3, 0x5E, 0x45, 0xD9, 0x47,
    0xAA, 0x59, 0xB6, 0x8F, 0x42, 0x66, 0x42, 0x29,
    0x90, 0x95, 0x48, 0x46, 0x91, 0x88, 0x3C, 0x8C,
    0xDE, 0xCC, 0xED, 0xB3, 0xAA, 0x62, 0xEA, 0xBC,
    0xB4, 0x0C, 0x48, 0x4C, 0x53, 0x23, 0x5E, 0x24,
    0x85, 0xBF, 0x92, 0xDA, 0x14, 0xDB, 0x1A, 0x3D,
    0xEF, 0x30, 0xD9, 0x49, 0x64, 0x4D, 0xE5, 0x01,
    0xFC, 0xA4, 0x4B, 0xD1, 0x9F, 0xDE, 0x96, 0x7F,
    0x50, 0xBC, 0x4D, 0x38, 0x44, 0xE9, 0x23, 0x5F,
    0x37, 0x57, 0x1A, 0xA6, 0x52, 0x5A, 0x0F, 0x4F,
    0x87, 0x33, 0x4A, 0x7B, 0x66, 0xEE, 0x3D, 0x66,
    0x0A, 0x63, 0x39, 0x1F, 0x23, 0x38, 0x35, 0x73,
    0x60, 0x5E, 0x47, 0x20, 0x4F, 0xC0, 0xC8, 0x3C,
    0x09, 0xF9, 0x29, 0x4F, 0x5E, 0x55, 0x69, 0xC4,
    0x6B, 0xE8, 0xF8, 0x91, 0xC0, 0x22, 0x65, 0x15,
    0x1E, 0xFB, 0xB9, 0x61, 0xCE, 0x45, 0xBE, 0x2B,
    0xEE, 0xB9, 0x04, 0x2B, 0xFD, 0xAE, 0x61, 0x1C,
    0x3D, 0x3D, 0x7C, 0xBF, 0xC1, 0xF7, 0x3C, 0x4E,
    0x9E, 0x0E, 0x54, 0xC8, 0xAD, 0xA9, 0xDF, 0x43,
    0x49, 0xB9, 0x41, 0x05, 0xE5, 0xF1, 0x49, 0xAA,
    0x77, 0x6C, 0x34, 0x5B, 0x93, 0x24, 0x24, 0x23,
    0x74, 0x68, 0x11, 0xCE, 0x15, 0x80, 0xA1, 0xA4,
    0x1F, 0x8D, 0x81, 0xCD, 0xB2, 0x98, 0xCA, 0x14,
    0x0B, 0x0C, 0x61, 0x50, 0x69, 0x72, 0xAE, 0xFA,
    0x8B, 0xC0, 0x3F, 0x0D, 0xE7, 0xF2, 0x0F, 0xEB,
    0xC1, 0x11, 0xB9, 0x10, 0x03, 0x6A, 0xF5, 0x97,
    0x3C, 0x53, 0x2F, 0x67, 0x86, 0x09, 0x6A, 0xE3,
    0x28, 0xC0, 0x78, 0xC8, 0xB4, 0x39, 0x8E, 0xD1,
    0xCE, 0x25, 0xE8, 0x66, 0xF7, 0x09, 0x40, 0x7D,
    0x81, 0xFB, 0xAF, 0xFA, 0x59, 0xC4, 0x9B, 0x2B,
    0x83, 0x45, 0x5B, 0xA8, 0x66, 0x9E, 0x38, 0xC8,
    0xFD, 0xAC, 0xF2, 0x2D, 0x21, 0xDE, 0x50, 0x4C,
    0x03, 0xCB, 0x88, 0x42, 0xDD, 0x84, 0x09, 0x99,
    0x8E, 0x8B, 0x40, 0x97, 0x1B, 0x14, 0x85, 0x37,
    0x11, 0x01, 0xE0, 0x74, 0x6B, 0x33, 0x52, 0x8C,
    0x68, 0x3A, 0x89, 0xB2, 0xAF, 0x35, 0xE6, 0x65,
    0xC3, 0x58, 0x70, 0xD2, 0xE7, 0x1F, 0x1F, 0xF6,
    0xE5, 0x0A, 0xB1, 0xFE, 0xD0, 0xC9, 0x51, 0x50,
    0xE7, 0xFD, 0x58, 0xF5, 0xC4, 0x58, 0x65, 0x94,
    0xD1, 0x57, 0x55, 0x5E, 0xD2, 0x27, 0x98, 0xAF,
    0xE7, 0x55, 0x0B, 0x87, 0x50, 0x9B, 0xEF, 0xE8,
    0x2B, 0xFC, 0xE7, 0x3B, 0x4E, 0xD7, 0xB7, 0x4D,
    0xF4, 0xBC, 0xF4, 0x88, 0x63, 0xE4, 0x8A, 0x20,
    0x4B, 0x22, 0xB0, 0xA0, 0x53, 0x7F, 0xA8, 0xC9,
    0x0C, 0xF8, 0xD7, 0xBD, 0x46, 0x39, 0xA7, 0x7D,
    0xDD, 0x10, 0x91, 0x50, 0x54, 0x06, 0x47, 0xF0,
    0x3C, 0xAA, 0x43, 0x40, 0xF8, 0x54, 0xDD, 0x8A,
    0xEA, 0x8A, 0x0B, 0xA5, 0x7F, 0xCD, 0x5E, 0xAA,
    0x02, 0x2E, 0x1F, 0xC6, 0x50, 0x15, 0xF8, 0x0A,
    0x0C, 0x1B, 0x3C, 0x55, 0x3A, 0xC3, 0x6F, 0x88,
    0xD7, 0xBF, 0xB1, 0x02, 0xCC, 0xE0, 0x08, 0x29,
    0x97, 0xD2, 0xAA, 0x23, 0xC4, 0x6D, 0xE3, 0xE3,
    0x76, 0x39, 0x92, 0xC3, 0x2E, 0x7A, 0xE2, 0x98,
    0xD1, 0xFC, 0xAE, 0xCC, 0x95, 0xD8, 0xB4, 0xDC,
    0x92, 0xEA, 0x6A, 0x5F, 0xF2, 0x92, 0x17, 0x0B,
    0x8D, 0xC3, 0xFA, 0x9C, 0x62, 0xCE, 0x44, 0x8D,
    0xC3, 0x1E, 0xC3, 0xB2, 0xD5, 0x00, 0xCD, 0xB4,
    0x9E, 0x2D, 0x7B, 0xF2, 0x98, 0xA3, 0x00, 0x8B,
    0x81, 0x30, 0x77, 0x5B, 0x02, 0x99, 0xB1, 0xCD,
    0xC3, 0x1D, 0x74, 0x74, 0xEF, 0x41, 0xCB, 0x69,
    0x63, 0x8E, 0xA6, 0xD3, 0x2D, 0x3E, 0x1F, 0x1D,
    0x12, 0x9E, 0xD9, 0x18, 0x67, 0x06, 0xAF, 0x37,
    0x29, 0xAD, 0x65, 0xD8, 0xEB, 0x71, 0xC4, 0x7D,
    0x94, 0x3D, 0xEA, 0xCC, 0xDF, 0x72, 0x41, 0x51,
    0x3C, 0xA1, 0x66, 0x98, 0x32, 0x32, 0x40, 0x54,
    0xB0, 0x2F, 0xEB, 0xCE, 0xDF, 0x4A, 0x64, 0xFB,
    0x9A, 0x90, 0xDC, 0xF6, 0x6F, 0xA9, 0xD4, 0xCA,
    0xCB, 0x91, 0xC4, 0xFE, 0xEE, 0x9C, 0x01, 0x50,
    0x2E, 0xAC, 0xCC, 0x5F, 0x89, 0xD0, 0x91, 0xA3,
    0xD9, 0xF9, 0x4B, 0x8D, 0xDE, 0x6C, 0x60, 0x21,
    0x19, 0xB1, 0xD3, 0x4D, 0x75, 0x56, 0x6F, 0xB8,
    0x25, 0xA4, 0x92, 0x4F, 0x12, 0xF5, 0x8F, 0xC1,
    0x17, 0x4B, 0xB3, 0x34, 0x21, 0x22, 0xAC, 0x52,
    0xD2, 0x64, 0xC9, 0x9A, 0x7D, 0xFC, 0xC0, 0x0A,
    0x89, 0x34, 0xFF, 0x08, 0xD3, 0x04, 0xDC, 0xFE,
    0x7C, 0xB3, 0xB8, 0xFD, 0x85, 0xDD, 0x79, 0x51,
    0xA7, 0x89, 0xE8, 0xF1, 0x23, 0xB1, 0xDF, 0xD7,
    0x1F, 0x7B, 0xB1, 0x5D, 0x42, 0xF9, 0x61, 0xF8,
    0xDC, 0x81, 0x04, 0xF1, 0xCC, 0xFA, 0xD7, 0xED,
    0xBF, 0x47, 0xAC, 0xBD, 0xE5, 0xFA, 0xAC, 0xB3,
    0x1C, 0xD9, 0xA1, 0xB3, 0x60, 0xEE, 0x9C, 0x8A,
    0x36, 0x57, 0xB4, 0x2F, 0xA1, 0xA2, 0xF3, 0xE2,
    0x09, 0x9A, 0x6E, 0x43, 0x9B, 0xE5, 0x93, 0xB8,
    0x3D, 0x9E, 0x9F, 0xC1, 0xC6, 0x0D, 0x02, 0xEB,
    0x4D, 0x38, 0xE9, 0xB4, 0x9F, 0xEA, 0x33, 0x8C,
    0x07, 0xD8, 0xB4, 0x71, 0xAD, 0xE5, 0x43, 0xB2,
    0xCC, 0x55, 0x93, 0x6A, 0xDB, 0x1E, 0x80, 0xDB,
    0xC2, 0xEA, 0x42, 0x8E, 0xFC, 0x86, 0x44, 0xC9,
    0x8A, 0xC4, 0xF2, 0x46, 0xA7, 0x39, 0x50, 0x0D,
    0x1A, 0xAA, 0x07, 0x04, 0xBE, 0xD4, 0xCE, 0x62,
    0x4D, 0x0F, 0x91, 0x7D, 0x29, 0x88, 0x9C, 0x4C,
    0xAF, 0xF7, 0xD8, 0x40, 0x93, 0x88, 0xC7, 0x20,
    0xD2, 0x17, 0x2A, 0xC4, 0x92, 0x72, 0xD0, 0xC0,
    0x4E, 0x56, 0x47, 0xB1, 0x27, 0x02, 0xE6, 0x61,
    0x82, 0x5E, 0xC8, 0x2E, 0x90, 0xD2, 0x31, 0x22,
    0xE2, 0xA9, 0x4A, 0x91, 0x45, 0x69, 0xB1, 0xA5,
    0x0F, 0x66, 0x2C, 0x30, 0xAD, 0x7F, 0x1B, 0x0E,
    0x22, 0x17, 0x60, 0x2E, 0x3D, 0x7F, 0x7F, 0x8C,
    0x33, 0x51, 0xA0, 0x25, 0xDE, 0xFD, 0x75, 0xBC,
    0xEF, 0xE6, 0xE7, 0x20, 0x04, 0x5A, 0xEC, 0x50,
    0x21, 0x48, 0x56, 0x98, 0xE2, 0x33, 0x6D, 0x22,
    0x5C, 0xC3, 0xFB, 0xFC, 0x6F, 0xB3, 0xA7, 0x8E,
    0x6F, 0x67, 0x70, 0x9D, 0xDA, 0x02, 0x01, 0x59,
    0x7B, 0x3D, 0x2B, 0x38, 0xCC, 0x0F, 0x44, 0x3D,
    0xFB, 0x9A, 0xB3, 0x23, 0x15, 0x50, 0x6E, 0xBF,
    0x8B, 0xA1, 0x94, 0x33, 0xE5, 0x7B, 0x88, 0x4E,
    0xCB, 0x6D, 0x9F, 0xBF, 0xBC, 0x7A, 0xA8, 0x1E,
    0x68, 0x25, 0xED, 0x8E, 0x53, 0x21, 0x72, 0xC5,
    0x70, 0xB3, 0xE4, 0xA6, 0xA1, 0x5A, 0x2D, 0xC8,
    0x43, 0x9D, 0x60, 0x77, 0x78, 0xE0, 0xC4, 0xAF,
    0xC8, 0x29, 0xBA, 0xD0, 0x4D, 0x39, 0x83, 0x51,
    0xA7, 0x10, 0x7F, 0x0C, 0x34, 0x0E, 0x6C, 0x75,
    0x26, 0xD7, 0xD6, 0xC7, 0x32, 0x53, 0xAF, 0x4E,
    0xBE, 0xF2, 0xC2, 0x0F, 0x99, 0x23, 0xB9, 0xE1,
    0xC8, 0xB4, 0xBC, 0x5A, 0xC6, 0xCB, 0xEB, 0x4D,
    0x28, 0x56, 0x72, 0xFE, 0x1B, 0x2C, 0x5D, 0xE3,
    0xBC, 0xC7, 0xA3, 0xC0, 0x7D, 0x27, 0xF0, 0xD0,
    0x4F, 0x3F, 0x1F, 0xF7, 0x87, 0x15, 0xF2, 0xEA,
    0xD4, 0x03, 0x6D, 0x2F, 0xD4, 0x8E, 0x50, 0x4B,
    0x05, 0xBF, 0xF7, 0x8C, 0x67, 0x5A, 0xDC, 0x4D,
    0xCD, 0xCF, 0x9D, 0x02, 0xB6, 0xE7, 0xAE, 0x49,
    0xD1, 0x7C, 0x00, 0xE7, 0x3B, 0xEA, 0xFB, 0x0D,
    0x2A, 0x7B, 0x41, 0x33, 0x66, 0xD0, 0x29, 0x9F,
    0xB3, 0x8A, 0x71, 0xB0, 0xE2, 0x76, 0xA9, 0xDB,
    0xFD, 0x64, 0x04, 0x69, 0xDF, 0x89, 0x1F, 0x56,
    0x86, 0x92, 0xD9, 0xD9, 0xB9, 0xF3, 0x4F, 0xAC,
    0xAE, 0x61, 0x48, 0x20, 0xCE, 0x3C, 0x2B, 0x44,
    0xAB, 0x42, 0xFA, 0xAB, 0x2E, 0x94, 0x82, 0xC8,
    0xD9, 0x97, 0xCF, 0x27, 0xDF, 0xAC, 0xAC, 0xE7,
    0xCA, 0xB2, 0x84, 0xAB, 0xF2, 0x5D, 0xDF, 0x56,
    0x0C, 0x8C, 0x07, 0x3C, 0x3D, 0xA8, 0xDD, 0xBE,
    0xFF, 0x4E, 0x28, 0x0D, 0xB2, 0x2D, 0xE6, 0x9D,
    0x44, 0x21, 0xCB, 0xE7, 0x33, 0x63, 0x22, 0x8F,
    0x4C, 0xFF, 0xB6, 0x1D, 0x9A, 0x71, 0x3F, 0xB1,
    0x29, 0xAE, 0x3A, 0x35, 0xEE, 0x9C, 0x97, 0x68,
    0xA7, 0x52, 0x66, 0x01, 0xD8, 0x9A, 0x5D, 0xF4,
    0xB3, 0x2F, 0x5C, 0xD4, 0x0E, 0xF9, 0xCF, 0x07,
    0xF6, 0x8C, 0xBA, 0xA6, 0x8D, 0x6B, 0xC6, 0x01,
    0xC2, 0x69, 0xAE, 0x60, 0x08, 0x1A, 0x0E, 0x3F,
    0xAE, 0x60, 0x29, 0xF3, 0x48, 0x0D, 0xE0, 0xD0,
    0xAE, 0x52, 0x44, 0xE9, 0x7F, 0x1F, 0x92, 0x5F,
    0x71, 0xAD, 0xEC, 0x6B, 0x47, 0x66, 0x92, 0x22,
    0x27, 0xAE, 0x6E, 0x25, 0xCD, 0xF3, 0x5F, 0x55,
    0x59, 0xBD, 0x73, 0xCE, 0x2B, 0x7E, 0x99, 0x44,
    0x56, 0x70, 0xA3, 0xE7, 0x7A, 0x59, 0x75, 0xD8,
    0x48, 0x0C, 0x39, 0x2B, 0xD7, 0x53, 0xC6, 0xAD,
    0x4A, 0x6F, 0xB4, 0x14, 0x96, 0xDF, 0xF2, 0x4A,
    0x0C, 0xA2, 0xD5, 0x29, 0x98, 0x7C, 0x42, 0x87,
    0xD9, 0x1F, 0x97, 0x61, 0xD9, 0xBF, 0x99, 0x4F,
    0x2C, 0x4C, 0x75, 0xAC, 0xB8, 0x06, 0x75, 0xD6,
    0x87, 0x76, 0x7E, 0xE3, 0x23, 0x4B, 0xEA, 0x1A,
    0x1A, 0xF4, 0xB7, 0x09, 0xAF, 0x53, 0xEB, 0xA6,
    0x39, 0x10, 0xFE, 0xD4, 0xEB, 0x1B, 0xAE, 0x38,
    0x31, 0x33, 0xBA, 0x68, 0xEE, 0xC7, 0x65, 0x76,
    0xFB, 0x49, 0x77, 0xD4, 0x19, 0xC4, 0xE6, 0xA7,
    0x05, 0xFE, 0x2A, 0xDA, 0x39, 0x99, 0x1A, 0x92,
    0xD2, 0xF0, 0x61, 0x97, 0xF6, 0x06, 0x6C, 0x88,
    0x7B, 0x6F, 0x60, 0xE6, 0x70, 0x08, 0xF0, 0xB4,
    0x6B, 0x39, 0x6F, 0x05, 0x41, 0x81, 0xF9, 0xBE,
    0x7A, 0x51, 0xC4, 0x75, 0xB0, 0x6A, 0x89, 0xA0,
    0xA6, 0x9A, 0x5B, 0xEE, 0x7D, 0x78, 0x17, 0x5F,
    0x9F, 0x3B, 0x7D, 0xDD, 0x8A, 0x9E, 0xAA, 0x1A,
    0xDA, 0x49, 0x08, 0xE9, 0xFD, 0x91, 0xA6, 0xFA,
    0xCE, 0xCF, 0x67, 0xDF, 0x0F, 0xC9, 0xD6, 0x38,
    0xD9, 0xD5, 0xD1, 0xC0, 0x76, 0x59, 0x42, 0x53,
    0xBF, 0x48, 0xE9, 0x11, 0x74, 0xC7, 0x11, 0xD8,
    0xE7, 0x8E, 0xD3, 0xC8, 0x25, 0xA1, 0x26, 0x50,
    0xBB, 0xB4, 0x35, 0xAF, 0xAF, 0x06, 0x23, 0x69,
    0x3E, 0x30, 0xFD, 0x7B, 0x34, 0x83, 0x07, 0xD0,
    0xF0, 0x0F, 0x6C, 0x9A, 0x13, 0x5D, 0xC2, 0x7B,
    0xDF, 0x6F, 0xDD, 0x8E, 0xF4, 0x30, 0x82, 0x05,
    0x41, 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7,
    0x0D, 0x01, 0x07, 0x01, 0xA0, 0x82, 0x05, 0x32,
    0x04, 0x82, 0x05, 0x2E, 0x30, 0x82, 0x05, 0x2A,
    0x30, 0x82, 0x05, 0x26, 0x06, 0x0B, 0x2A, 0x86,
    0x48, 0x86, 0xF7, 0x0D, 0x01, 0x0C, 0x0A, 0x01,
    0x02, 0xA0, 0x82, 0x04, 0xEE, 0x30, 0x82, 0x04,
    0xEA, 0x30, 0x1C, 0x06, 0x0A, 0x2A, 0x86, 0x48,
    0x86, 0xF7, 0x0D, 0x01, 0x0C, 0x01, 0x03, 0x30,
    0x0E, 0x04, 0x08, 0x74, 0xC0, 0x84, 0x8F, 0xC7,
    0x74, 0x5E, 0x21, 0x02, 0x02, 0x08, 0x00, 0x04,
    0x82, 0x04, 0xC8, 0x1E, 0xF4, 0xE9, 0x07, 0x27,
    0x9E, 0x5A, 0xC9, 0x39, 0x1D, 0x37, 0x2C, 0x06,
    0x4B, 0x57, 0xEA, 0xC5, 0x42, 0x9A, 0x60, 0xD5,
    0x42, 0xB2, 0x34, 0x2D, 0xD3, 0x88, 0x7C, 0x78,
    0x87, 0xB6, 0xE9, 0x42, 0x44, 0x1F, 0x67, 0x32,
    0x92, 0x54, 0x22, 0xDA, 0xB2, 0x43, 0xE7, 0x40,
    0xBE, 0x1F, 0xAF, 0x3A, 0xCD, 0x2A, 0x9F, 0xD7,
    0x44, 0x5B, 0x37, 0x69, 0x85, 0xDF, 0xEB, 0x2A,
    0xB9, 0xE2, 0x92, 0x3B, 0xEA, 0xD5, 0x42, 0x53,
    0x95, 0x4A, 0xB0, 0x1B, 0xA5, 0xEF, 0xA6, 0x0D,
    0x29, 0xF4, 0x33, 0xFE, 0xD7, 0x49, 0x04, 0x1E,
    0x8C, 0xAD, 0x63, 0x1E, 0x79, 0x63, 0x74, 0x0C,
    0xE5, 0x5E, 0xA2, 0x2C, 0xBE, 0xB8, 0x90, 0xCE,
    0x06, 0x25, 0xBF, 0xD1, 0x5A, 0x50, 0xCF, 0x3B,
    0x52, 0xE2, 0xA7, 0xFF, 0x19, 0x02, 0xCF, 0xD0,
    0x9B, 0xD9, 0xF7, 0x28, 0x07, 0x38, 0x1F, 0xF2,
    0xAF, 0x44, 0x91, 0x3F, 0x0F, 0xB6, 0x6E, 0x8C,
    0xC0, 0x32, 0x92, 0xC0, 0xCD, 0x25, 0x98, 0x67,
    0xF1, 0x47, 0x52, 0x50, 0xF0, 0xA3, 0x7B, 0xE6,
    0x74, 0xDC, 0x72, 0x28, 0xC8, 0xAB, 0xB3, 0x31,
    0x7D, 0xA3, 0xF7, 0xC7, 0xD1, 0xE6, 0x99, 0xB4,
    0xB6, 0x5A, 0x3A, 0x4D, 0x83, 0x4F, 0xB8, 0xB5,
    0x86, 0xF8, 0x37, 0x7F, 0xA0, 0x16, 0x2F, 0x3C,
    0x62, 0x7A, 0xD4, 0x3A, 0xEB, 0xC2, 0xE8, 0x03,
    0x49, 0x17, 0x9E, 0xFB, 0xD7, 0xAF, 0x91, 0x32,
    0xFD, 0xEA, 0x4F, 0x64, 0xC6, 0x6E, 0x02, 0xEA,
    0xC4, 0xC8, 0x1F, 0x16, 0xC5, 0x4C, 0xFB, 0xC5,
    0x42, 0xF5, 0x85, 0x05, 0x92, 0x59, 0x4B, 0x31,
    0xE5, 0xE9, 0x69, 0xE7, 0x02, 0x98, 0x33, 0xBA,
    0x4C, 0x17, 0x09, 0xEF, 0x89, 0x20, 0xFA, 0x83,
    0x9F, 0xAE, 0x0E, 0x1B, 0x7D, 0x98, 0xB9, 0xF2,
    0x3C, 0x0F, 0xB7, 0x1C, 0x72, 0xDF, 0x17, 0x84,
    0x7F, 0x0A, 0xFD, 0x12, 0x3C, 0x6F, 0x68, 0x5D,
    0x45, 0xEB, 0xB8, 0xD6, 0x24, 0x65, 0x42, 0x75,
    0x5C, 0xC2, 0xF3, 0x3A, 0x6A, 0x4E, 0x51, 0x34,
    0x1B, 0xB6, 0x81, 0xB2, 0x8A, 0xEF, 0x28, 0xA4,
    0xC5, 0x88, 0x9A, 0x97, 0xE0, 0xEF, 0x31, 0x12,
    0x01, 0x7E, 0x1B, 0x43, 0x0F, 0x27, 0x80, 0x87,
    0x98, 0xC5, 0xD5, 0x83, 0xCB, 0x4B, 0xB7, 0x01,
    0x79, 0x60, 0xA1, 0x1A, 0x03, 0x05, 0xC6, 0x36,
    0x04, 0x31, 0x3C, 0x06, 0xDB, 0x08, 0xA8, 0xDA,
    0x8E, 0x32, 0x19, 0x91, 0xF1, 0x0D, 0x61, 0x6F,
    0xE4, 0xB2, 0x79, 0x8A, 0xDE, 0xF4, 0xF7, 0xFB,
    0x2C, 0x23, 0x5B, 0xD9, 0x64, 0x2F, 0xB7, 0xB3,
    0x8B, 0xCA, 0xB8, 0x8C, 0x1D, 0x3B, 0x49, 0x05,
    0x38, 0xA1, 0xE5, 0x8C, 0x1A, 0xDC, 0xA5, 0x61,
    0xFE, 0xF4, 0x2B, 0xDC, 0x77, 0x28, 0xF6, 0x19,
    0xE7, 0xB7, 0x8F, 0x4D, 0x27, 0x2D, 0xED, 0x8A,
    0x3F, 0x3D, 0xDC, 0x9F, 0xD1, 0x30, 0xFF, 0xD6,
    0xC3, 0xBE, 0x41, 0x25, 0xE3, 0xA5, 0x9B, 0x73,
    0xDF, 0x6A, 0xD9, 0xF9, 0x70, 0x84, 0x02, 0x4C,
    0x35, 0xD4, 0x3E, 0x05, 0x76, 0x3A, 0xDC, 0x6D,
    0x5A, 0x81, 0xB3, 0x94, 0xF7, 0x22, 0xF7, 0xDC,
    0xC1, 0x43, 0x31, 0x57, 0x5B, 0x42, 0x9A, 0x0B,
    0xF4, 0x95, 0x30, 0xA9, 0xBB, 0xD8, 0x06, 0xFB,
    0x1D, 0x6F, 0x9B, 0xC3, 0xBB, 0xF3, 0xBF, 0xFB,
    0xB4, 0x9F, 0x35, 0x64, 0x0A, 0x69, 0xB7, 0xD1,
    0x3E, 0xCA, 0x78, 0x07, 0x04, 0x03, 0x79, 0xD4,
    0xF3, 0xA8, 0xEC, 0x18, 0xDB, 0x03, 0x5E, 0x47,
    0xD7, 0xD0, 0x56, 0x2C, 0x74, 0x94, 0x86, 0x04,
    0x46, 0xB8, 0xD4, 0x35, 0x0A, 0x7B, 0xE6, 0x78,
    0xC4, 0x43, 0x3C, 0x56, 0xCC, 0x37, 0x8B, 0xFD,
    0xE8, 0xF4, 0x57, 0xEA, 0xAE, 0xCF, 0x36, 0x97,
    0x12, 0xAC, 0x39, 0xCF, 0x7C, 0xEF, 0x22, 0x67,
    0x01, 0xEC, 0xD8, 0x09, 0x49, 0x4E, 0xE3, 0x74,
    0xDD, 0x39, 0xE1, 0x39, 0xD7, 0x0C, 0x5F, 0x1B,
    0xCE, 0x69, 0xBC, 0x72, 0x44, 0x87, 0x64, 0x1C,
    0x08, 0x05, 0x93, 0x69, 0x6D, 0x7F, 0x90, 0x0A,
    0x2C, 0xCB, 0x8A, 0xBB, 0x7F, 0xE3, 0xE0, 0x80,
    0x31, 0xD0, 0x0A, 0x3A, 0x95, 0xFF, 0xF7, 0xB4,
    0x36, 0x38, 0x93, 0xE0, 0x0C, 0x11, 0x37, 0x12,
    0x06, 0xF6, 0xAD, 0xE9, 0xB1, 0x7A, 0x00, 0xF5,
    0xD2, 0x32, 0x6B, 0xD0, 0x27, 0xA5, 0x1B, 0x3D,
    0xE8, 0xDB, 0xCC, 0xA9, 0x1F, 0x1F, 0xB1, 0x99,
    0x3D, 0x7C, 0xB7, 0xCA, 0xDA, 0x27, 0x2C, 0x64,
    0x1C, 0x49, 0xB6, 0x87, 0x44, 0x06, 0x94, 0x9D,
    0xBC, 0x6B, 0x20, 0xA2, 0x68, 0x15, 0x1F, 0xE2,
    0xF2, 0xAD, 0x6D, 0x23, 0x2E, 0x2B, 0x74, 0xE2,
    0x5D, 0xE4, 0xB0, 0xC7, 0x84, 0xCB, 0x64, 0xBF,
    0xE0, 0xA8, 0x18, 0x83, 0xB4, 0xC9, 0xD9, 0x73,
    0xA8, 0xE6, 0xA9, 0x36, 0xD5, 0x63, 0x1E, 0x2C,
    0x2A, 0x55, 0x09, 0x77, 0x5E, 0xB3, 0x4B, 0xEA,
    0xB5, 0xD0, 0x14, 0x5F, 0xEB, 0x50, 0x7B, 0xAA,
    0xEF, 0x94, 0xBA, 0x2B, 0xD7, 0x8A, 0x07, 0xF1,
    0xF9, 0x5E, 0x12, 0x12, 0x21, 0x52, 0xE5, 0x0A,
    0x3E, 0xC0, 0xBC, 0x5D, 0x4C, 0xE2, 0x12, 0x7C,
    0x39, 0xF9, 0x16, 0x9D, 0xBD, 0x96, 0x83, 0x3B,
    0x7F, 0x3D, 0x6A, 0xEC, 0xF1, 0x25, 0xD2, 0xB0,
    0xB0, 0xEB, 0x20, 0x06, 0x07, 0xD6, 0xD9, 0x4C,
    0x07, 0x9A, 0x82, 0xC1, 0xFC, 0xF7, 0x66, 0x15,
    0xBD, 0x62, 0x65, 0xD8, 0x6C, 0xF6, 0x33, 0x7B,
    0x5A, 0x28, 0xEC, 0x90, 0xA1, 0x26, 0x9F, 0xC3,
    0x28, 0x4A, 0x64, 0x50, 0x5F, 0xCA, 0xE2, 0x6D,
    0xB8, 0x0F, 0xE2, 0x94, 0xB5, 0x8E, 0x1F, 0x8A,
    0x8F, 0x6B, 0xA6, 0x86, 0x1F, 0xEE, 0xDC, 0x24,
    0xB4, 0xB8, 0x25, 0xEC, 0x28, 0x2D, 0xF9, 0xCB,
    0x7D, 0x38, 0xFF, 0xC7, 0x74, 0x2E, 0xD3, 0x10,
    0xEC, 0x03, 0x31, 0xEE, 0x83, 0xE7, 0xA4, 0xF7,
    0xBA, 0x28, 0x21, 0xE0, 0x7F, 0xB4, 0xB7, 0xE1,
    0x7A, 0xF9, 0x2B, 0xB0, 0x2C, 0x3B, 0x80, 0x5F,
    0xE0, 0x5D, 0xB2, 0x7E, 0x59, 0xFF, 0x59, 0x07,
    0x58, 0x42, 0x57, 0xEE, 0x44, 0xF1, 0xB1, 0xAD,
    0xBA, 0xDE, 0xCB, 0x1D, 0x8A, 0x36, 0x67, 0xE8,
    0x45, 0xFF, 0x07, 0x8D, 0xEE, 0xA4, 0x51, 0x9C,
    0x4C, 0x83, 0x5D, 0x2E, 0x2F, 0xE1, 0x5B, 0x75,
    0xE8, 0x29, 0xCD, 0x0B, 0x07, 0x62, 0xE0, 0xC3,
    0x0D, 0x1D, 0xEA, 0xCF, 0xF0, 0x8A, 0x65, 0x27,
    0x70, 0x42, 0x9F, 0x26, 0x00, 0x15, 0x70, 0xC5,
    0x4A, 0xF6, 0x25, 0xD0, 0x40, 0x72, 0xE9, 0xC1,
    0x73, 0xFD, 0x48, 0x94, 0xA3, 0x8D, 0x66, 0x63,
    0x96, 0x4F, 0xF7, 0xEE, 0xFB, 0x4C, 0xC7, 0xB8,
    0x6B, 0xE9, 0x90, 0xE1, 0x2A, 0x66, 0x80, 0x99,
    0x3B, 0xB0, 0x1A, 0x6C, 0xF9, 0x0E, 0x72, 0xDA,
    0x8E, 0x4F, 0x46, 0xC2, 0x6A, 0x4B, 0x7A, 0x16,
    0xE5, 0x26, 0x0B, 0x5C, 0xD4, 0x47, 0x34, 0xE5,
    0x37, 0xBE, 0x68, 0x6C, 0xDA, 0xD3, 0x9B, 0x6F,
    0xAE, 0x51, 0x9C, 0x99, 0x0A, 0x5B, 0xF8, 0x37,
    0xBC, 0xDE, 0xFC, 0x93, 0xC5, 0xE7, 0x0F, 0xEF,
    0x0B, 0xA6, 0x07, 0xC2, 0xA6, 0xE6, 0xDA, 0x2D,
    0x1B, 0x49, 0xC9, 0xDE, 0x6B, 0x27, 0xDC, 0x00,
    0xEF, 0x23, 0x87, 0x0E, 0xEB, 0xD1, 0x48, 0x7D,
    0xB4, 0xF2, 0x58, 0xC6, 0x3C, 0xE2, 0x89, 0xBA,
    0xB0, 0x05, 0xAC, 0x94, 0x41, 0x9A, 0xA8, 0xFF,
    0x3E, 0xBC, 0x3A, 0x52, 0x9C, 0xF9, 0x7F, 0x07,
    0x8B, 0xB0, 0x2C, 0x71, 0x83, 0x7B, 0xCF, 0x2E,
    0x7F, 0x7C, 0x96, 0x65, 0xD9, 0x08, 0x17, 0xEC,
    0xFA, 0xDE, 0x4E, 0x40, 0x12, 0x26, 0x70, 0x71,
    0x65, 0xA5, 0xDC, 0x98, 0x47, 0xA3, 0xFC, 0xE0,
    0x9A, 0x16, 0xED, 0x45, 0x56, 0x72, 0x50, 0x05,
    0x28, 0x2C, 0x99, 0xEC, 0x20, 0x2E, 0x40, 0xC0,
    0x26, 0x69, 0xCD, 0x49, 0x45, 0x17, 0xA4, 0xA3,
    0x42, 0x0D, 0x14, 0x65, 0x87, 0x33, 0x8C, 0x92,
    0xC5, 0xC4, 0x61, 0xFD, 0xE8, 0x68, 0x56, 0x20,
    0x57, 0xF5, 0x8E, 0x5F, 0xCF, 0x7E, 0x97, 0xF6,
    0x49, 0x97, 0x0A, 0xFE, 0xD3, 0x60, 0x1A, 0x5B,
    0x0C, 0x75, 0xDD, 0x8E, 0x31, 0x78, 0x29, 0xA6,
    0xB1, 0x4D, 0xAA, 0xDF, 0x8A, 0xD1, 0xE6, 0x91,
    0xE3, 0x32, 0x3F, 0xEC, 0x8A, 0x1F, 0x0E, 0x35,
    0x07, 0x6E, 0x4B, 0x83, 0x3B, 0xE5, 0x67, 0x34,
    0x1F, 0x0C, 0x81, 0xD8, 0xD5, 0x25, 0x68, 0xE5,
    0x28, 0x1B, 0x5C, 0x81, 0x3E, 0xE3, 0x5C, 0xB4,
    0xB6, 0xBD, 0x62, 0x6A, 0x70, 0x33, 0xC2, 0xC5,
    0x75, 0x27, 0xF4, 0x30, 0xE1, 0x1D, 0xC1, 0x4C,
    0xC5, 0x02, 0x12, 0x46, 0xAC, 0xEC, 0xF9, 0xE8,
    0xE7, 0x58, 0x24, 0x11, 0xB1, 0xF3, 0xB7, 0x8C,
    0x3C, 0xA4, 0x0A, 0x94, 0xA6, 0x7C, 0x68, 0x54,
    0x5B, 0xB9, 0x4D, 0x57, 0x9C, 0xE7, 0x28, 0x09,
    0x6B, 0x89, 0x26, 0x5D, 0xE7, 0x50, 0xA9, 0x95,
    0x90, 0x91, 0x8E, 0x00, 0x59, 0xF8, 0x3A, 0x70,
    0xAF, 0x48, 0x2E, 0xE8, 0xC4, 0x34, 0x8C, 0xF4,
    0x5F, 0x7F, 0xCB, 0x07, 0xAA, 0xF0, 0xD9, 0xFB,
    0x5C, 0x32, 0x90, 0x22, 0x1A, 0xD2, 0x1A, 0xCF,
    0x92, 0x06, 0x02, 0xCF, 0x10, 0x18, 0x7B, 0x93,
    0xCC, 0x07, 0x4A, 0x31, 0x25, 0x30, 0x23, 0x06,
    0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01,
    0x09, 0x15, 0x31, 0x16, 0x04, 0x14, 0xD1, 0xDE,
    0x23, 0x16, 0x9F, 0x6E, 0xF4, 0x42, 0x21, 0x23,
    0xE1, 0x11, 0xAA, 0xC8, 0x7C, 0x60, 0x4A, 0x78,
    0x9D, 0x24, 0x30, 0x31, 0x30, 0x21, 0x30, 0x09,
    0x06, 0x05, 0x2B, 0x0E, 0x03, 0x02, 0x1A, 0x05,
    0x00, 0x04, 0x14, 0xD6, 0x4A, 0xBB, 0x75, 0xB1,
    0xF9, 0x9E, 0xD3, 0x58, 0x6D, 0xD1, 0x74, 0x9F,
    0x00, 0x8A, 0xF2, 0xC8, 0xAA, 0x52, 0x4D, 0x04,
    0x08, 0x77, 0x46, 0xE7, 0xBA, 0x25, 0x4B, 0xDA,
    0x41, 0x02, 0x02, 0x08, 0x00]
};

const WPA_EAP_CLIENT_LIST = [
  {
    ssid: 'WPA-EAP-PEAP',
    keyManagement: 'WPA-EAP',
    eap: 'PEAP',
    identity: EAP_USERNAME,
    password: EAP_PASSWORD,
    serverCertificate: CLIENT_PKCS12_CERT.nickname,
    phase2: 'MSCHAPV2'
  }
];

/**
 * Convert the given MozWifiNetwork object array to testAssociate chain.
 *
 * @param aNetworks
 *        An array of MozWifiNetwork which we want to convert.
 *
 * @return A promise chain which "then"s testAssociate accordingly.
 */
function convertToTestAssociateChain(aNetworks) {
  let chain = Promise.resolve();

  aNetworks.forEach(function (aNetwork) {
    network = new window.MozWifiNetwork(aNetwork);
    chain = chain.then(() => gTestSuite.testAssociate(network));
  });

  return chain;
}

gTestSuite.doTestWithCertificate(
  new Blob([new Uint8Array(CLIENT_PKCS12_CERT.content)]),
  CLIENT_PKCS12_CERT.password,
  CLIENT_PKCS12_CERT.nickname,
  CLIENT_PKCS12_CERT.usage,
  function() {
    return gTestSuite.ensureWifiEnabled(true)
      // Load required server files.
      .then(() => gTestSuite.writeFile(SERVER_EAP_USER_CONF.path, SERVER_EAP_USER_CONF.content))
      .then(() => gTestSuite.writeFile(CA_CERT.path, CA_CERT.content))
      .then(() => gTestSuite.writeFile(SERVER_CERT.path, SERVER_CERT.content))
      .then(() => gTestSuite.writeFile(SERVER_KEY.path, SERVER_KEY.content))
      // Start AP.
      .then(() => gTestSuite.startHostapds(WPA_EAP_AP_LIST))
      // Scan test.
      .then(() => gTestSuite.testWifiScanWithRetry(SCAN_RETRY_CNT, WPA_EAP_AP_LIST))
      // Associate test.
      .then(() => convertToTestAssociateChain(WPA_EAP_CLIENT_LIST))
      // Tear down.
      .then(gTestSuite.killAllHostapd)
});
