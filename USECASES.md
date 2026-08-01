# Use cases

## Add a Let's Encrypt trust anchor

Use this procedure when an HTTPS resource presents a certificate chain rooted
at ISRG Root X1 and fails with certificate-validation stage `4`, detail
`-9984` (`MBEDTLS_ERR_X509_CERT_VERIFY_FAILED`).

Download the ISRG Root X1 certificate in DER format and create a PEM copy for
inspection:

```sh
curl http://x1.i.lencr.org/ --output ./ISRG_Root_X1.der && \
openssl x509 -in ISRG_Root_X1.der -out ISRG_Root_X1.pem \
  -inform der -outform pem
```

Because the download URL uses HTTP, verify the certificate subject, issuer,
validity, CA constraints, and SHA-256 fingerprint against an authoritative
Let's Encrypt source before trusting it:

```sh
openssl x509 -in ISRG_Root_X1.pem -noout \
  -subject -issuer -dates -fingerprint -sha256 -text
```

Upload the original DER file to the device using an administrator bearer
token:

```sh
curl -sk -X POST https://<device>/api/v1/trust-anchors \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/octet-stream" \
  --data-binary @ISRG_Root_X1.der
```

The response contains the assigned persistent anchor ID:

```json
{"id":1}
```

Assign that ID to the resource. Replace `<index>` with the resource index from
`GET /api/v1/health-check/config`:

```sh
curl -sk -X PUT https://<device>/api/v1/health-check/resources/<index> \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  --data '{"trust_anchor_id":1}'
```

The next scheduled check should complete certificate validation and write its
result to the health-check log. Restart the board afterward and confirm that
both the trust anchor and the resource association remain available.
