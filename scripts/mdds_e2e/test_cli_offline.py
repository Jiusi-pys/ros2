"""Host oracles only: no ROS import, board access or matrix mutation."""
import base64
import copy
import datetime
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from cli_offline_common import command_plan, check_packages, check_bag, verify_signed, check_security, policy_xml


def artifact(data):
    return {'kind': 'file', 'sha256': hashlib.sha256(data).hexdigest(),
            'data': base64.b64encode(data).decode()}

class OfflinePlanTests(unittest.TestCase):
    def test_plan_contains_exact_nine_offline_ids_and_explicit_owned_destinations(self):
        plan = command_plan('/run/cli_metadata/work', 'offline_run')
        self.assertEqual({'cli:pkg/create', 'cli:bag/list'} |
          {'cli:security/' + name for name in ('create_keystore', 'create_enclave', 'create_key',
            'create_permission', 'generate_artifacts', 'list_enclaves', 'list_keys')}, set(plan))
        for commands in plan.values():
            for argv in commands:
                self.assertEqual('ros2', argv[0])
                self.assertNotIn('--help', argv)
                self.assertNotIn('generate_policy', argv)
        self.assertEqual(8, len(plan['cli:bag/list']))
        self.assertEqual(2, len(plan['cli:pkg/create']))
        self.assertIn('--maintainer-email', plan['cli:pkg/create'][0])
        self.assertIn('/run/cli_metadata/work/keystore', plan['cli:security/create_keystore'][0])

    def test_bag_output_is_checked_against_all_real_plugin_fields(self):
        plugin = {'name': 'sqlite3', 'type': 'SqliteStorage', 'base': 'ReadWriteInterface', 'description': 'sqlite'}
        second = dict(plugin, name='mcap', type='MCAPStorage', description='mcap')
        context = {'bag_plugins': {'storage': [plugin, second], 'converter': [dict(plugin, name='converter')],
                                  'compressor': [dict(plugin, name='zstd')], 'decompressor': [dict(plugin, name='zstd')]}}
        observations = []
        for kind, plugins in context['bag_plugins'].items():
            observations.append({'stdout': ''.join(x['name'] + '\n' for x in plugins)})
            observations.append({'stdout': 'available ' + kind + ' plugins are:\n' + ''.join(
                f"name: {x['name']}\n\t{x['description']}\n\ttype: {x['type']}\n\tbase_class: {x['base']}\n" for x in plugins)})
        check_bag(observations, context)
        bad = copy.deepcopy(observations);bad[0]['stdout'] = 'sqlite3\n'
        with self.assertRaises(ValueError):check_bag(bad, context)
        bad = copy.deepcopy(observations);bad[1]['stdout'] = bad[1]['stdout'].replace('MCAPStorage', 'Wrong')
        with self.assertRaises(ValueError):check_bag(bad, context)

    def test_package_oracle_rejects_missing_scaffolding_and_wrong_metadata(self):
        with self.assertRaises(ValueError):check_packages({'files': {}}, {'run_id': 'offline_run'})

class SignatureOracleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from cryptography import x509
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.hazmat.primitives.serialization import pkcs7
        key = ec.generate_private_key(ec.SECP256R1())
        name = x509.Name([x509.NameAttribute(x509.oid.NameOID.COMMON_NAME, 'offline test CA')])
        now = datetime.datetime.utcnow()
        cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
                .serial_number(x509.random_serial_number()).not_valid_before(now - datetime.timedelta(days=1))
                .not_valid_after(now + datetime.timedelta(days=1))
                .add_extension(x509.BasicConstraints(ca=True, path_length=1), critical=True).sign(key, hashes.SHA256()))
        cls.ca = cert.public_bytes(serialization.Encoding.PEM)
        cls.xml = b'<dds><fixture>content</fixture></dds>\n'
        cls.signed = (pkcs7.PKCS7SignatureBuilder().set_data(cls.xml).add_signer(cert, key, hashes.SHA256())
                      .sign(serialization.Encoding.SMIME, [pkcs7.PKCS7Options.Text, pkcs7.PKCS7Options.DetachedSignature]))
        cls.openssl = Path(__file__).resolve().parents[2] / '.pixi/envs/default/Library/bin/openssl.exe'
        assert cls.openssl.is_file(), 'the explicitly checked host OpenSSL runtime is required'

    def test_real_detached_signature_and_unsigned_payload_both_must_match(self):
        with tempfile.TemporaryDirectory() as directory:
            proof = verify_signed(self.ca, self.signed, self.xml, self.openssl, Path(directory))
            self.assertEqual(0, proof.get('returncode'))
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                verify_signed(self.ca, self.signed, b'<other/>\n', self.openssl, Path(directory))

    def test_tampered_smime_must_not_pass(self):
        tampered = self.signed.replace(b'content', b'changed', 1)
        self.assertNotEqual(tampered, self.signed)
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                verify_signed(self.ca, tampered, self.xml, self.openssl, Path(directory))


class SecurityArtifactTests(unittest.TestCase):
    def setUp(self):
        from cryptography import x509
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.hazmat.primitives.serialization import pkcs7
        from lxml import etree
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.openssl = Path(__file__).resolve().parents[2] / '.pixi/envs/default/Library/bin/openssl.exe'
        self.identity = '/offline_run/alpha'
        ca_key, leaf_key = ec.generate_private_key(ec.SECP256R1()), ec.generate_private_key(ec.SECP256R1())
        name = x509.Name([x509.NameAttribute(x509.oid.NameOID.COMMON_NAME, 'sros2CA')])
        now = datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0)
        def certificate(subject, key, ca):
            return (x509.CertificateBuilder().subject_name(subject).issuer_name(name).public_key(key.public_key())
                    .serial_number(x509.random_serial_number()).not_valid_before(now - datetime.timedelta(days=1))
                    .not_valid_after(now + datetime.timedelta(days=1))
                    .add_extension(x509.BasicConstraints(ca=ca, path_length=1 if ca else None), critical=ca)
                    .sign(ca_key, hashes.SHA256()))
        ca = certificate(name, ca_key, True)
        leaf = certificate(x509.Name([x509.NameAttribute(x509.oid.NameOID.COMMON_NAME, self.identity)]), leaf_key, False)
        self.sign = lambda xml: (pkcs7.PKCS7SignatureBuilder().set_data(xml).add_signer(ca, ca_key, hashes.SHA256())
            .sign(serialization.Encoding.SMIME, [pkcs7.PKCS7Options.Text, pkcs7.PKCS7Options.DetachedSignature]))
        source = Path(__file__).resolve().parents[2] / 'src/ros2/sros2/sros2/sros2/policy'
        governance = etree.parse(str(source / 'defaults/dds/governance.xml'))
        governance.find('domain_access_rules/domain_rule/domains/id').text = '53'
        self.gov = etree.tostring(governance, pretty_print=True)
        transform = etree.XSLT(etree.parse(str(source / 'templates/dds/permissions.xsl')))
        permissions = transform(etree.fromstring(policy_xml([self.identity])),
            not_valid_before=etree.XSLT.strparam(leaf.not_valid_before.isoformat()),
            not_valid_after=etree.XSLT.strparam(leaf.not_valid_after.isoformat()))
        permissions.find('permissions/grant/allow_rule/domains/id').text = '53'
        xml = etree.tostring(permissions, pretty_print=True)
        ca_bytes = ca.public_bytes(serialization.Encoding.PEM)
        prefix = 'enclaves/' + self.identity.lstrip('/') + '/'
        self.prefix = prefix
        self.capture = {'files': {}, 'keys': {}}
        def add(name, data, resolved=None):
            item = artifact(data);item['resolved'] = resolved or name
            self.capture['files'][name] = item
        add('public/ca.cert.pem', ca_bytes)
        for name in ('identity', 'permissions'): add('public/' + name + '_ca.cert.pem', ca_bytes, 'public/ca.cert.pem')
        add('enclaves/governance.xml', self.gov); add('enclaves/governance.p7s', self.sign(self.gov))
        add(prefix + 'cert.pem', leaf.public_bytes(serialization.Encoding.PEM))
        for name in ('identity', 'permissions'): add(prefix + name + '_ca.cert.pem', ca_bytes, 'public/ca.cert.pem')
        add(prefix + 'governance.p7s', self.capture['files']['enclaves/governance.p7s'] and
            base64.b64decode(self.capture['files']['enclaves/governance.p7s']['data']), 'enclaves/governance.p7s')
        add(prefix + 'permissions.xml', xml); add(prefix + 'permissions.p7s', self.sign(xml))
        for path, key in [(x, ca_key) for x in ('private/ca.key.pem', 'private/identity_ca.key.pem', 'private/permissions_ca.key.pem')] + [(prefix + 'key.pem', leaf_key)]:
            public = key.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
            self.capture['keys'][path] = {'algorithm': 'EC', 'curve': 'secp256r1', 'mode': 0o600,
                                          'public_key_sha256': hashlib.sha256(public).hexdigest()}

    def verify(self):
        return check_security(self.capture, [self.identity], {self.identity}, 53, self.openssl, self.directory)

    def test_valid_public_certificates_permissions_signatures_and_private_audit_pass(self):
        self.assertEqual(2, len(self.verify()))
        self.capture['keys'][self.prefix + 'key.pem']['public_key_sha256'] = '0' * 64
        with self.assertRaises(ValueError):self.verify()

    def test_validly_signed_weakened_topic_governance_is_not_accepted(self):
        weakened = self.gov.replace(b'<data_protection_kind>ENCRYPT</data_protection_kind>',
                                    b'<data_protection_kind>NONE</data_protection_kind>')
        self.assertNotEqual(weakened, self.gov)
        self.capture['files']['enclaves/governance.xml'] = artifact(weakened)
        signed = self.sign(weakened)
        self.capture['files']['enclaves/governance.p7s'] = artifact(signed)
        self.capture['files'][self.prefix + 'governance.p7s'] = artifact(signed)
        with self.assertRaises(ValueError):self.verify()



class OfflineReceiptTests(unittest.TestCase):
    def test_printed_pass_with_nonzero_real_cli_exit_produces_only_failed_receipts(self):
        import cli_acceptance as acceptance
        from board_cli_metadata import write_json
        from board_cli_offline import capture_marker, execution_text
        from verify_cli_offline_archive import build_receipts
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'captured';root.mkdir()
            run_id = 'offline_unit'
            board = acceptance.TARGET['board_serials'][0]
            template = acceptance.make_manifest(acceptance.discover_inventory(
                Path(__file__).resolve().parents[2] / 'install_ohos'))
            work = '/data/local/tmp/ros2/.mdds-owned-runs/' + run_id + '/cli_metadata/work'
            plan = command_plan(work, run_id)
            context = {'schema_version': 1, 'run_id': run_id, 'board_serial': board,
                       'prefix': '/data/local/tmp/ros2', 'work': work, 'domain': 53,
                       'commands': plan, 'inventory': template['inventory'], 'python_executable': '/bin/python3.12',
                       'physical_dsoftbus_proven': False}
            diagnostics = {'rmw': {'returncode': 0, 'stdout': 'rmw_mdds\n'}, 'help': {}}
            candidates = []
            for case_id, commands in plan.items():
                capture = write_json(root / (case_id.replace(':', '_').replace('/', '_') + '.capture.json'),
                                     {'files': {}, 'keys': {}, 'errors': []})
                marker = capture_marker(run_id, case_id, capture['sha256'])
                observations = []
                diagnostics['help'][case_id] = {'returncode': 0, 'timed_out': False,
                    'argv': ['ros2'] + case_id.removeprefix('cli:').split('/') + ['--help']}
                for i, argv in enumerate(commands):
                    observation = {'argv': argv, 'actual_argv': ['/bin/python3.12', '-B', '-c',
                        'from ros2cli.cli import main; raise SystemExit(main())'] + argv[1:],
                        'returncode': 1, 'stdout': 'PASS\n', 'stderr': 'simulated real failure',
                        'timed_out': False, 'board_serial': board}
                    name = case_id.replace(':', '_').replace('/', '_') + '.' + str(i) + '.log'
                    (root / name).write_text(execution_text(observation, run_id, case_id, board, marker if i == 0 else ''), encoding='utf-8')
                    observation['log'] = {'path': name, 'sha256': acceptance.digest((root / name).read_bytes())}
                    observations.append(observation)
                candidates.append({'id': case_id, 'capture': capture, 'executions': observations})
            context_ref = write_json(root / 'oracle_context.json', context)
            diagnostic_ref = write_json(root / 'diagnostics.json', diagnostics)
            write_json(root / 'candidates.json', {'schema_version': 1, 'run_id': run_id,
                       'oracle_context': context_ref, 'diagnostics': diagnostic_ref, 'cases': candidates})
            output = Path(directory) / 'evidence'
            openssl = Path(__file__).resolve().parents[2] / '.pixi/envs/default/Library/bin/openssl.exe'
            summary = build_receipts(root, output, template, run_id, openssl)
            self.assertEqual(0, summary['passed_cases'])
            self.assertEqual(9, len(summary['failed_cases']))
            manifest = json.loads((output / 'partial_manifest.json').read_text())
            self.assertEqual(9, sum(c['status'] == 'FAIL' for c in manifest['cases']))
            self.assertEqual(89, sum(c['status'] == 'NOT_RUN' for c in manifest['cases']))
            gate = json.loads((output / 'phase_gate.json').read_text())
            self.assertFalse(gate['gateway_unlocked'])
            self.assertEqual(98, gate['required_cases'])


if __name__ == '__main__':unittest.main()
