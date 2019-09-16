---
author: @mcpiroman
created on: 2019-09-16
last updated: 2019-09-16
issue id: 2131
---

HTML copy customization

## Abstract

HTML copy allows to embed a fragment of terminal's content into other applications like text editors in formated way as opposed to plain text. By default it resembles current terminal's look which is usually but not always desirable.

This does not describe settings for other copy formats, like upcoming RTF. Although some settings might be applicable, I want to keep them separated for greater flexibility.

## Inspiration

Common case is when you write a document / web page interleaved with terminal snippets and want it to have consistent style. Although you may be able to style each such snippet (or maybe even do some bulk edit), this usually requires quite a bit of additional efford.

Secondly, this settings will allow you to keep your actual terminal style distinct from the copied one (so you don't have your anime background in a report that goes to your profesor/boss); some properties are not adjustable in this way.

## Duplicated settings

Following settings are simple optional duplicates of similar settings in terminal that affect the HTML output rather then terminal itself. Their names begin with `html` (e.g. `fontSize` -> `htmlFontSize`), type remain the same. When not set, they default to the 'original' ones.

1. `htmlPadding`
2. `fontFace`
3. `fontSize`
4. `colorTable`


## Dedicated settings

These settings are special to HTML:

1. `htmlWidth`
- Description: Controlls width of HTML output.
- Type: `string`
- Possible values:
   - `"terminal"` - the width of the actual terminal window when copy was made.
   - `"matchSelection"` - whole selected area.
   - `"matchText"` - within range of selected printable character, so trim blank spaces.
   - `css value` - a value that will be pasted into `width` property in main element's style attribute. As implementing a css validation should be hard, we'd simply assume it's css if it doesn't match any of the values above.

## Capabilities

[comment]: # Discuss how the proposed fixes/features impact the following key considerations:

### Accessibility

[comment]: # How will the proposed change impact accessibility for users of screen readers, assistive input devices, etc.

### Security

[comment]: # How will the proposed change impact security?

### Reliability

[comment]: # Will the proposed change improve reliabilty? If not, why make the change?

### Compatibility

[comment]: # Will the proposed change break existing code/behaviors? If so, how, and is the breaking change "worth it"?

### Performance, Power, and Efficiency

## Potential Issues

[comment]: # What are some of the things that might cause problems with the fixes/features proposed? Consider how the user might be negatively impacted.

## Future considerations

[comment]: # What are some of the things that the fixes/features might unlock in the future? Does the implementation of this spec enable scenarios?

## Resources

[comment]: # Be sure to add links to references, resources, footnotes, etc.
