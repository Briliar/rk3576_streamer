const toggles = document.querySelectorAll('input[type="checkbox"]');

toggles.forEach((input) => {
  input.addEventListener('change', () => {
    const label = input.parentElement?.innerText.trim() || 'unknown';
    console.log(`${label}: ${input.checked ? 'on' : 'off'}`);
  });
});
