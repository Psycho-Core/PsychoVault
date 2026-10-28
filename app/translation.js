'use strict'

const LANGUAGES = Object.freeze({
  en: 'English',
  es: 'Spanish',
  fr: 'French',
  de: 'German',
  it: 'Italian',
  pt: 'Portuguese',
  nl: 'Dutch',
  ar: 'Arabic',
  'zh-CN': 'Chinese (Simplified)',
  ja: 'Japanese',
  ko: 'Korean',
  hi: 'Hindi',
  ru: 'Russian',
  el: 'Greek',
  tr: 'Turkish',
  vi: 'Vietnamese',
  th: 'Thai',
  id: 'Indonesian',
  ms: 'Malay',
  tl: 'Filipino'
})

function buildTranslationUrl(pageUrl, targetLanguage) {
  let source
  try { source = new URL(String(pageUrl || '')) }
  catch { return { ok: false, error: 'Open a web page before translating.' } }

  if (source.protocol !== 'http:' && source.protocol !== 'https:') {
    return { ok: false, error: 'Only web pages can be translated.' }
  }
  if (source.username || source.password) {
    return { ok: false, error: 'This URL contains embedded credentials and cannot be sent to a translation service.' }
  }

  const language = String(targetLanguage || 'en')
  const languageName = Object.prototype.hasOwnProperty.call(LANGUAGES, language) ? LANGUAGES[language] : null
  if (!languageName) return { ok: false, error: 'Choose a supported translation language in Settings.' }

  const translated = new URL('https://translate.google.com/translate')
  translated.searchParams.set('sl', 'auto')
  translated.searchParams.set('tl', language)
  translated.searchParams.set('hl', language)
  translated.searchParams.set('u', source.href)
  return { ok: true, url: translated.href, language, languageName, source: source.href }
}

module.exports = { LANGUAGES, buildTranslationUrl }
