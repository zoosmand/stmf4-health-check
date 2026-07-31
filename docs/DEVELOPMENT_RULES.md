# Development rules

This document defines the contribution workflow for project-owned firmware,
documentation, build configuration, and tests. It applies to maintainers,
contributors, and automated coding assistants.

## Issue-first development

Every functional change, correction, or substantial refactor starts with a
GitHub issue. The issue must describe:

- the problem or goal;
- the intended scope;
- important constraints and exclusions;
- acceptance and verification criteria;
- known follow-up work that is deliberately deferred.

Add a readable issue comment when implementation begins or when a design
decision materially changes the scope. Do not expose passwords, tokens,
private keys, or other credentials in issues or comments.

Small documentation-only changes may be committed directly to `dev` only when
the maintainer explicitly authorizes it.

## Branches

Create the implementation branch from an up-to-date `dev` branch. Its name
must use the corresponding issue number:

```text
issue-<number>
```

For example, issue 10 is implemented in `issue-10`.

Do not mix unrelated work into an issue branch. If local work was started on
the wrong branch, preserve tracked and untracked files with a named stash,
create the correct branch, and pop the stash there. Resolve any conflicts
before continuing.

## Code and documentation

- Follow [the project naming conventions](NAMING_CONVENTIONS.md).
- Keep imported vendor and middleware code in its upstream style.
- Preserve the established ownership of `Core`, `Periph`, `Srv`, `LWIP`, and
  `TLS`.
- Document public interfaces, structures, units, blocking behavior, ownership,
  and task or interrupt restrictions where relevant.
- Keep comments focused on design intent, hardware constraints, and non-obvious
  behavior.
- Update the README or focused documentation whenever externally visible
  behavior, setup, commands, diagnostics, or dependencies change.
- Do not delete unused vendor source trees merely to reduce the build. Control
  compiled sources through the Makefile.

Third-party source trees excluded by `.gitignore` must be installed according
to [the ignored-source instructions](IGNORED_SOURCES.md).

## Local and generated files

The `.vscode/` configuration belongs to the maintainer. Contributors must ask
before changing any file inside it, even when an editor suggests an automatic
update.

Do not commit:

- build products or logs;
- editor, assistant, or machine-specific configuration;
- plaintext passwords, bearer tokens, private keys, or certificates containing
  private keys;
- generated files that embed secrets;
- unrelated local changes.

A generated password verifier, public certificate, salt, or other non-secret
configuration may be committed when the design explicitly requires it. Verify
that no plaintext source credential is present before committing.

## Verification

Before requesting review:

1. Inspect the complete diff and confirm that it contains only issue-related
   changes.
2. Run `git diff --check`.
3. Perform a clean firmware build using the repository Makefile.
4. Review compiler and linker warnings rather than dismissing them
   automatically.
5. Record Flash, ordinary SRAM, and CCM RAM changes when they are material.
6. Run available host-side tests and static checks.
7. Test on the target device when hardware behavior is affected.

The maintainer performs final target-device checks unless they explicitly
delegate them. Do not report hardware verification unless it actually ran.
Non-critical vendor-library warnings may be accepted only after their impact
has been reviewed and documented.

## Commits and publication

- Do not commit or push until the maintainer requests it.
- Use a concise imperative commit subject describing the outcome.
- Keep commits scoped to the issue.
- Never rewrite shared history without explicit approval.
- Publish only the current issue branch unless direct `dev` publication was
  explicitly authorized.

## Pull requests

Open the pull request from `issue-<number>` into `dev`. Its description or first
comment should summarize:

- the implemented behavior;
- important design decisions;
- verification performed;
- memory impact when relevant;
- known limitations and deferred work;
- the corresponding issue.

Read every review finding and verify it against the current code. Fix findings
that affect correctness, security, reliability, maintainability, or meaningful
resource usage. If a recommendation should not be implemented, explain the
technical reason clearly in the pull request rather than silently ignoring it.

After changes, commit and push them only when requested, then add a readable PR
comment describing what changed and how it was verified.

## Merge and cleanup

Merge only after review approval and successful required testing. Use the merge
method requested by the maintainer.

After a successful merge:

1. Comment the corresponding issue with the final result.
2. Close the issue when all accepted scope is complete.
3. Delete the issue branch when requested.
4. Return to `dev`.
5. Pull the merged `dev` branch.

Do not close an issue when required work remains. Record deferred work in a new
issue or explicitly document it as an accepted limitation.

