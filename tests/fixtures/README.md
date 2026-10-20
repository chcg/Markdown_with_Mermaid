# Test fixtures

`linked-document.md` is a project-authored fixture for local Markdown links.
`navigation-a.md` through `navigation-d.md` exercise history branches,
cross-document anchors, scroll restoration and modified open buffers.

`mermaid-logo.svg` is the unmodified Mermaid favicon, copied from
`assets/icons/mermaid.svg`. It is also byte-identical to the favicon from the
Mermaid documentation at commit `1fb7869866dd619168d840291c9c54b4f129758b`:
https://github.com/mermaid-js/mermaid/blob/1fb7869866dd619168d840291c9c54b4f129758b/docs/public/favicon.svg

Mermaid copyright and MIT terms are preserved in `vendor/LICENSE-Mermaid.txt`.
The fixture does not change the bundled rendering engine version, 11.16.1.

The regular tests require only the files shipped in this source tree. Large
third-party documentation snapshots and private export samples are excluded.
The integration host also accepts optional external regression documents;
these are additional tests and are not prerequisites for the standard run.
