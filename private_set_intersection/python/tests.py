import re
import sys
from math import ceil, floor

import pytest

import private_set_intersection.python as psi

client_key = bytes(range(32))
server_key = bytes(range(1, 33))
fpr = 0.01
num_client_inputs = 10
num_server_inputs = 100
client_items = ["Element " + str(i) for i in range(num_client_inputs)]
server_items = ["Element " + str(2 * i) for i in range(num_server_inputs)]


def test_version():
    version = psi.__version__
    assert re.match(r"[0-9]+[.][0-9]+[.][0-9]+(-[A-Za-z0-9]+)?", version)


def test_static_key():
    c = psi.client.CreateFromKey(client_key, False)
    assert c.GetPrivateKeyBytes() == client_key

    s = psi.server.CreateFromKey(server_key, False)
    assert s.GetPrivateKeyBytes() == server_key


def _exchange(c, s, ds):
    setup = psi.ServerSetup()
    setup.ParseFromString(
        s.CreateSetupMessage(
            fpr, len(client_items), server_items, ds
        ).SerializeToString()
    )

    request = psi.Request()
    request.ParseFromString(c.CreateRequest(client_items).SerializeToString())

    response = psi.Response()
    response.ParseFromString(s.ProcessRequest(request).SerializeToString())

    return setup, response


def _assert_exact_intersection(c, setup, response):
    iset = set(c.GetIntersection(setup, response))
    for idx in range(len(client_items)):
        if idx % 2 == 0:
            assert idx in iset
        else:
            assert idx not in iset


@pytest.mark.parametrize("reveal_intersection", [False, True])
@pytest.mark.parametrize(
    "ds", [psi.DataStructure.RAW, psi.DataStructure.GCS, psi.DataStructure.BLOOM_FILTER]
)
def test_integration(ds, reveal_intersection):
    # Fixed keys, not CreateWithNewKey. GCS and BLOOM_FILTER are probabilistic
    # membership structures whose false-positive rate is the `fpr` argument to
    # CreateSetupMessage, so under fresh random keys the "non-member is absent"
    # assertion fails on roughly `fpr` of runs -- measured at these parameters as
    # 0.7% for GCS and 1.5% for BLOOM_FILTER, independent of any defect. Fixing
    # the keys fixes the encrypted elements, and with them the filter contents
    # and every membership answer, so all three structures can assert exact
    # membership with no tolerance. Only RAW carries no false-positive rate.
    c = psi.client.CreateFromKey(client_key, reveal_intersection)
    s = psi.server.CreateFromKey(server_key, reveal_intersection)

    setup, response = _exchange(c, s, ds)

    if reveal_intersection:
        _assert_exact_intersection(c, setup, response)
    else:
        intersection = c.GetIntersectionSize(setup, response)
        assert intersection >= floor(len(client_items) / 2.0)
        assert intersection <= ceil((len(client_items) / 2.0) * 1.1)


def test_integration_new_key():
    # Covers generated keys end to end, which test_integration's fixed keys do
    # not reach. RAW because its membership is exact for any key, so a freshly
    # generated one cannot make this assertion flake.
    c = psi.client.CreateWithNewKey(True)
    s = psi.server.CreateWithNewKey(True)

    setup, response = _exchange(c, s, psi.DataStructure.RAW)

    _assert_exact_intersection(c, setup, response)


if __name__ == "__main__":
    sys.exit(pytest.main(["-s", "-v", "-x", __file__]))
