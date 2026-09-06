#!/usr/bin/env python3
"""Offline command plans and independent public-artifact oracles."""
import ast
import base64
from collections import Counter
import hashlib
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import tempfile
import xml.etree.ElementTree as ET

MAX_FILE = 1024 * 1024
KINDS = ('storage', 'converter', 'compressor', 'decompressor')


def sha(data): return hashlib.sha256(data).hexdigest()


def identity_root(run_id):
    return '/offline_' + re.sub('[^a-z0-9_]', '_', run_id.lower())


def command_plan(work, run_id):
    work = str(work).replace('\\', '/').rstrip('/')
    base = identity_root(run_id)
    result = {}
    result['cli:pkg/create'] = []
    for suffix, build, dep in [('cpp', 'ament_cmake', 'rclcpp'), ('py', 'ament_python', 'rclpy')]:
        result['cli:pkg/create'].append(['ros2', 'pkg', 'create', 'mdds_offline_' + suffix,
            '--build-type', build, '--destination-directory', work + '/packages',
            '--license', 'Apache-2.0', '--node-name', 'probe_node',
            '--description', 'Offline acceptance ' + run_id, '--maintainer-name', 'MDDS Offline',
            '--maintainer-email', 'offline@example.invalid', '--dependencies', dep, 'std_msgs'])
    result['cli:bag/list'] = [['ros2', 'bag', 'list', kind] + extra
                             for kind in KINDS for extra in ([], ['--verbose'])]
    result['cli:security/create_keystore'] = [['ros2', 'security', 'create_keystore', work + '/keystore']]
    result['cli:security/create_enclave'] = [['ros2', 'security', 'create_enclave', work + '/keystore', base + '/alpha']]
    result['cli:security/create_key'] = [['ros2', 'security', 'create_key', work + '/keystore', base + '/beta']]
    result['cli:security/create_permission'] = [['ros2', 'security', 'create_permission', work + '/keystore', base + '/alpha', work + '/permission_policy.xml']]
    result['cli:security/generate_artifacts'] = [['ros2', 'security', 'generate_artifacts', '-k', work + '/generated_keystore', '-e', base + '/extra', '-p', work + '/artifacts_policy.xml']]
    for verb in ('list_enclaves', 'list_keys'):
        result['cli:security/' + verb] = [['ros2', 'security', verb, work + '/keystore']]
    return result


def policy_xml(identities):
    root = ET.Element('policy', version='0.2.0')
    enclaves = ET.SubElement(root, 'enclaves')
    for identity in identities:
        enclave = ET.SubElement(enclaves, 'enclave', path=identity)
        profile = ET.SubElement(ET.SubElement(enclave, 'profiles'), 'profile', ns=identity, node='node')
        ET.SubElement(ET.SubElement(profile, 'topics', publish='ALLOW', subscribe='ALLOW'), 'topic').text = 'topic'
        ET.SubElement(ET.SubElement(profile, 'services', request='ALLOW'), 'service').text = 'sum'
    return ET.tostring(root, encoding='utf-8', xml_declaration=True) + b'\n'


def read_public(record):
    if not isinstance(record, dict): raise ValueError('missing public artifact')
    try: data = base64.b64decode(record['data'], validate=True)
    except Exception as exc: raise ValueError('malformed public artifact encoding') from exc
    if len(data) > MAX_FILE or sha(data) != record.get('sha256') or b'PRIVATE KEY' in data:
        raise ValueError('public artifact hash/size/secret-content violation')
    return data


def capture_tree(root, security=False):
    root = Path(root).resolve()
    if not root.is_dir(): raise ValueError('command did not create its output tree')
    files, keys = {}, {}
    total = 0
    for path in sorted(root.rglob('*')):
        if path.is_dir() and not path.is_symlink(): continue
        relative = path.relative_to(root).as_posix()
        resolved = path.resolve(strict=True)
        try: final = resolved.relative_to(root).as_posix()
        except ValueError as exc: raise ValueError('generated link escapes owned output tree') from exc
        info = resolved.stat()
        if not stat.S_ISREG(info.st_mode) or info.st_size > MAX_FILE:
            raise ValueError('generated artifact is not a bounded regular file')
        data = resolved.read_bytes(); total += len(data)
        if total > 8 * MAX_FILE or len(files) + len(keys) >= 256:
            raise ValueError('generated artifact tree exceeded capture bounds')
        record = {'kind': 'symlink' if path.is_symlink() else 'file', 'resolved': final}
        if path.is_symlink(): record['target'] = os.readlink(path)
        if security and path.name.endswith('key.pem'):
            from cryptography.hazmat.primitives import serialization
            from cryptography.hazmat.primitives.asymmetric import ec
            key = serialization.load_pem_private_key(data, password=None)
            if not isinstance(key, ec.EllipticCurvePrivateKey): raise ValueError('unexpected private key algorithm')
            public = key.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
            record.update(algorithm='EC', curve=key.curve.name, public_key_sha256=sha(public), mode=stat.S_IMODE(info.st_mode))
            keys[relative] = record  # Never serialize private bytes or their text.
        else:
            if b'PRIVATE KEY' in data: raise ValueError('private key encountered in public capture')
            record.update(sha256=sha(data), data=base64.b64encode(data).decode())
            files[relative] = record
    return {'files': files, 'keys': keys}


def collect_plugins(prefix):
    prefix = Path(prefix)
    groups = {'storage': 'rosbag2_storage__pluginlib__plugin',
              'converter': 'rosbag2_cpp__pluginlib__plugin',
              'compressor': 'rosbag2_compression__pluginlib__plugin',
              'decompressor': 'rosbag2_compression__pluginlib__plugin'}
    result, evidence = {}, []
    for kind, group in groups.items():
        result[kind] = []
        for marker in sorted((prefix / 'share/ament_index/resource_index' / group).glob('*')):
            for name in marker.read_text().splitlines():
                if not name.strip(): continue
                source = prefix / name
                if not source.resolve().is_relative_to(prefix.resolve()): raise ValueError('plugin metadata escapes prefix')
                raw = source.read_bytes(); tree = ET.fromstring(raw)
                library = tree.attrib['path']
                library_file = prefix / 'lib' / ('lib' + library + '.so')
                if not library_file.is_file(): raise ValueError('listed plugin library missing: ' + library)
                with library_file.open('rb') as stream:
                    header = stream.read(20); stream.seek(0); library_sha = hashlib.file_digest(stream, 'sha256').hexdigest()
                if header[:6] != b'\x7fELF\x02\x01' or int.from_bytes(header[18:20], 'little') != 183:
                    raise ValueError('plugin library is not AArch64 ELF64')
                evidence.append({'xml': name, 'sha256': sha(raw), 'library': library_file.relative_to(prefix).as_posix(), 'library_sha256': library_sha})
                for item in tree.iter('class'):
                    base = item.attrib['base_class_type']
                    if kind == 'compressor' and base != 'rosbag2_compression::BaseCompressorInterface': continue
                    if kind == 'decompressor' and base != 'rosbag2_compression::BaseDecompressorInterface': continue
                    result[kind].append({'name': item.attrib['name'], 'type': item.attrib['type'], 'base': base,
                                         'description': item.findtext('description', '').strip()})
    return result, evidence


def check_bag(observations, context):
    if len(observations) != 8: raise ValueError('bag list must execute all four kinds/plain and verbose')
    plugins = context['bag_plugins']
    if not {'sqlite3', 'mcap'} <= {x['name'] for x in plugins['storage']}:
        raise ValueError('sqlite3 or mcap is missing')
    for x in plugins['storage']:
        if x['name'] in ('sqlite3', 'mcap') and 'ReadWriteInterface' not in x['base']:
            raise ValueError('required storage plugin lacks reader/writer interface')
    for index, kind in enumerate(KINDS):
        wanted = plugins[kind]
        if not wanted: raise ValueError('no actual plugin metadata for ' + kind)
        plain = observations[2 * index]['stdout'].split()
        if Counter(plain) != Counter(x['name'] for x in wanted): raise ValueError('bag plugin names differ: ' + kind)
        text = observations[2 * index + 1]['stdout']
        if not text.startswith('available ' + kind + ' plugins are:\n'): raise ValueError('wrong verbose plugin heading')
        actual = []
        for block in text.split('\nname: ')[1:]:
            lines = block.splitlines()
            if len(lines) < 4 or not lines[-2].startswith('\ttype: ') or not lines[-1].startswith('\tbase_class: '):
                raise ValueError('malformed verbose plugin record')
            actual.append((lines[0], lines[-2][7:], lines[-1][13:], '\n'.join(lines[1:-2]).strip()))
        expected = [(x['name'], x['type'], x['base'], x['description']) for x in wanted]
        if Counter(actual) != Counter(expected): raise ValueError('verbose plugin metadata differs: ' + kind)


def check_packages(capture, context):
    files = capture.get('files', {})
    for suffix, build, dep in [('cpp', 'ament_cmake', 'rclcpp'), ('py', 'ament_python', 'rclpy')]:
        package = 'mdds_offline_' + suffix
        def data(name): return read_public(files.get(package + '/' + name))
        tree = ET.fromstring(data('package.xml'))
        expected = {'name': package, 'version': '0.0.0', 'description': 'Offline acceptance ' + context['run_id'],
                    'maintainer': 'MDDS Offline', 'license': 'Apache-2.0', 'export/build_type': build}
        if (tree.tag != 'package' or tree.attrib.get('format') != '3' or
                any(tree.findtext(k) != v for k, v in expected.items()) or
                tree.find('maintainer').attrib.get('email') != 'offline@example.invalid' or
                {n.text for n in tree.findall('depend')} != {dep, 'std_msgs'}):
            raise ValueError('generated package metadata differs: ' + package)
        if b'Apache License' not in data('LICENSE'): raise ValueError('missing Apache license output')
        if suffix == 'cpp':
            cmake = data('CMakeLists.txt').decode()
            required = [f'project({package})', 'find_package(rclcpp REQUIRED)', 'find_package(std_msgs REQUIRED)',
                        'add_executable(probe_node src/probe_node.cpp)', 'ament_package()']
            if any(text not in cmake for text in required) or b'main' not in data('src/probe_node.cpp'):
                raise ValueError('generated C++ scaffold is incomplete')
        else:
            setup = data('setup.py').decode(); ast.parse(setup)
            node = ast.parse(data(package + '/probe_node.py').decode())
            if (f'probe_node = {package}.probe_node:main' not in setup or
                    not any(isinstance(n, ast.FunctionDef) and n.name == 'main' for n in node.body)):
                raise ValueError('generated Python entry point is missing')
            data('setup.cfg'); data('resource/' + package); data(package + '/__init__.py')


def verify_signed(ca, signed, unsigned, openssl, directory):
    directory = Path(directory)
    with tempfile.TemporaryDirectory(prefix='smime_', dir=directory) as temporary:
        root = Path(temporary)
        (root / 'ca.pem').write_bytes(ca); (root / 'signed.p7s').write_bytes(signed)
        argv = [str(openssl), 'smime', '-verify', '-text', '-binary', '-in', str(root / 'signed.p7s'),
                '-CAfile', str(root / 'ca.pem'), '-purpose', 'any', '-no-CApath', '-no-CAstore',
                '-out', str(root / 'verified.xml')]
        result = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
        proof = {'host_tool': 'openssl', 'argv': argv, 'returncode': result.returncode,
                 'stdout': result.stdout.decode(errors='replace'), 'stderr': result.stderr.decode(errors='replace'),
                 'signed_sha256': sha(signed), 'unsigned_sha256': sha(unsigned), 'ca_sha256': sha(ca)}
        if result.returncode != 0: raise ValueError('detached S/MIME signature verification failed')
        verified = (root / 'verified.xml').read_bytes()
        if verified.replace(b'\r\n', b'\n') != unsigned.replace(b'\r\n', b'\n'):
            raise ValueError('signed payload differs from the generated XML')
        return proof


def expected_security(case_id, run_id):
    base = identity_root(run_id)
    if case_id == 'cli:security/create_keystore': return [], set()
    if case_id == 'cli:security/create_enclave': return [base + '/alpha'], set()
    if case_id == 'cli:security/generate_artifacts':
        return [base + '/' + name for name in ('delta', 'extra', 'gamma')], {base + '/delta', base + '/gamma'}
    return [base + '/alpha', base + '/beta'], ({base + '/alpha'} if case_id in
        ('cli:security/create_permission', 'cli:security/list_enclaves', 'cli:security/list_keys') else set())


def check_security(capture, identities, custom, domain, openssl, directory):
    from cryptography import x509
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    files, keys = capture['files'], capture['keys']
    wanted_files = {'public/ca.cert.pem', 'public/identity_ca.cert.pem', 'public/permissions_ca.cert.pem',
                    'enclaves/governance.xml', 'enclaves/governance.p7s'}
    wanted_keys = {'private/ca.key.pem', 'private/identity_ca.key.pem', 'private/permissions_ca.key.pem'}
    for identity in identities:
        prefix = 'enclaves/' + identity.lstrip('/') + '/'
        wanted_files.update(prefix + name for name in ('cert.pem', 'identity_ca.cert.pem', 'permissions_ca.cert.pem', 'governance.p7s', 'permissions.xml', 'permissions.p7s'))
        wanted_keys.add(prefix + 'key.pem')
    if set(files) != wanted_files or set(keys) != wanted_keys: raise ValueError('keystore/enclave artifact set differs')
    def data(name): return read_public(files[name])
    def pub_sha(cert):
        return sha(cert.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo))
    ca_bytes = data('public/ca.cert.pem'); ca = x509.load_pem_x509_certificate(ca_bytes)
    if (ca.subject != ca.issuer or not ca.extensions.get_extension_for_class(x509.BasicConstraints).value.ca or
            ca.subject.get_attributes_for_oid(x509.oid.NameOID.COMMON_NAME)[0].value != 'sros2CA'):
        raise ValueError('invalid generated CA identity/constraints')
    ca.public_key().verify(ca.signature, ca.tbs_certificate_bytes, ec.ECDSA(ca.signature_hash_algorithm))
    for name in ('identity', 'permissions'):
        if data('public/' + name + '_ca.cert.pem') != ca_bytes: raise ValueError('CA alias content differs')
        if files['public/' + name + '_ca.cert.pem'].get('resolved') != 'public/ca.cert.pem': raise ValueError('CA link escapes expected target')
    for name in wanted_keys:
        item = keys[name]
        if item.get('algorithm') != 'EC' or item.get('curve') != 'secp256r1' or item.get('mode', 0o777) & 0o077:
            raise ValueError('private key audit failed without disclosing key material')
    for name in ('ca.key.pem', 'identity_ca.key.pem', 'permissions_ca.key.pem'):
        if keys['private/' + name].get('public_key_sha256') != pub_sha(ca): raise ValueError('CA/private-key public identity differs')
    governance = data('enclaves/governance.xml'); tree = ET.fromstring(governance)
    values = {'domains/id': str(domain), 'allow_unauthenticated_participants': 'false', 'enable_join_access_control': 'true',
              'discovery_protection_kind': 'ENCRYPT', 'liveliness_protection_kind': 'ENCRYPT', 'rtps_protection_kind': 'SIGN'}
    rules = tree.findall('domain_access_rules/domain_rule')
    if len(rules) != 1 or any(rules[0].findtext(k) != v for k, v in values.items()): raise ValueError('governance domain/protection differs')
    topic_rules = rules[0].findall('topic_access_rules/topic_rule')
    topic_values = {'topic_expression': '*', 'enable_discovery_protection': 'true',
                    'enable_liveliness_protection': 'true', 'enable_read_access_control': 'true',
                    'enable_write_access_control': 'true', 'metadata_protection_kind': 'ENCRYPT',
                    'data_protection_kind': 'ENCRYPT'}
    if len(topic_rules) != 1 or any(topic_rules[0].findtext(k) != v for k, v in topic_values.items()):
        raise ValueError('governance topic protections differ from the generated default')
    proof = [verify_signed(ca_bytes, data('enclaves/governance.p7s'), governance, openssl, directory)]
    for identity in identities:
        prefix = 'enclaves/' + identity.lstrip('/') + '/'
        cert = x509.load_pem_x509_certificate(data(prefix + 'cert.pem'))
        if (cert.issuer != ca.subject or cert.subject.get_attributes_for_oid(x509.oid.NameOID.COMMON_NAME)[0].value != identity or
                cert.extensions.get_extension_for_class(x509.BasicConstraints).value.ca or
                keys[prefix + 'key.pem'].get('public_key_sha256') != pub_sha(cert)):
            raise ValueError('enclave certificate/private-key identity mismatch')
        ca.public_key().verify(cert.signature, cert.tbs_certificate_bytes, ec.ECDSA(cert.signature_hash_algorithm))
        for name in ('identity', 'permissions'):
            if data(prefix + name + '_ca.cert.pem') != ca_bytes: raise ValueError('enclave CA link content differs')
        if data(prefix + 'governance.p7s') != data('enclaves/governance.p7s'): raise ValueError('enclave governance link differs')
        permissions = data(prefix + 'permissions.xml'); tree = ET.fromstring(permissions)
        grants = tree.findall('permissions/grant')
        if len(grants) != 1: raise ValueError('expected one enclave permissions grant')
        grant = grants[0]
        if grant.attrib.get('name') != identity or grant.findtext('subject_name') != 'CN=' + identity or grant.findtext('default') != 'DENY':
            raise ValueError('permission grant identity/default differs')
        allows = grant.findall('allow_rule')
        if len(allows) != 1 or grant.findall('deny_rule') or allows[0].findtext('domains/id') != str(domain):
            raise ValueError('permission rule/domain set differs')
        if identity in custom:
            publish = {'rt' + identity + '/topic', 'rq' + identity + '/sumRequest'}
            subscribe = {'rt' + identity + '/topic', 'rr' + identity + '/sumReply'}
        else:
            publish = {'rt/*', 'rq/*Request', 'rr/*Reply', 'rt/*/_action/feedback', 'rt/*/_action/status'}
            publish.update('rq/*/_action/' + op + 'Request' for op in ('cancel_goal', 'get_result', 'send_goal'))
            publish.update('rr/*/_action/' + op + 'Reply' for op in ('cancel_goal', 'get_result', 'send_goal'))
            subscribe = publish
        if ({n.text for n in allows[0].findall('publish/topics/topic')} != publish or
                {n.text for n in allows[0].findall('subscribe/topics/topic')} != subscribe):
            raise ValueError('translated permission topics differ from the fixture policy')
        if (grant.findtext('validity/not_before') != cert.not_valid_before.isoformat() or
                grant.findtext('validity/not_after') != cert.not_valid_after.isoformat()):
            raise ValueError('permission validity differs from its certificate')
        proof.append(verify_signed(ca_bytes, data(prefix + 'permissions.p7s'), permissions, openssl, directory))
    return proof
