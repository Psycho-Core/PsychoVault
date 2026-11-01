const { test } = require('node:test');
const assert = require('node:assert/strict');
const { destination } = require('../www/url.js');
test('URLs and searches', () => {
  assert.equal(destination('example.com:8080'), 'https://example.com:8080/');
  assert.equal(destination('localhost:8080'), 'https://localhost:8080/');
  assert.equal(destination('example.com'), 'https://example.com/');
  assert.equal(destination('https://example.com/a'), 'https://example.com/a');
  assert.equal(destination('hello there', 'duckduckgo'), 'https://duckduckgo.com/?q=hello%20there');
  assert.equal(destination('hello there', 'bing'), 'https://www.bing.com/search?q=hello%20there');
  assert.equal(destination('hello there'), 'https://www.google.com/search?q=hello%20there');
  for (const bad of ['', 'javascript:alert(1)', 'file:///etc/passwd', 'intent://foo', 'https://']) assert.equal(destination(bad), null);
});
