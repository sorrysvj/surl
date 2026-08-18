import ora, { Ora } from 'ora';

let activeSpinner: Ora | null = null;
let spinnerEnabled = true;

export function setSpinnerEnabled(enabled: boolean): void {
  spinnerEnabled = enabled;
}

export function createSpinner(text: string): Ora {
  if (!spinnerEnabled) {
    // Return a no-op spinner for CI/quiet mode
    return {
      start: () => activeSpinner as Ora,
      stop: () => activeSpinner as Ora,
      succeed: () => activeSpinner as Ora,
      fail: () => activeSpinner as Ora,
      warn: () => activeSpinner as Ora,
      info: () => activeSpinner as Ora,
      text: '',
    } as Ora;
  }

  // Stop any existing spinner
  if (activeSpinner) {
    activeSpinner.stop();
  }

  activeSpinner = ora({
    text,
    spinner: 'dots',
  });

  return activeSpinner;
}

export function startSpinner(text: string): Ora {
  const spinner = createSpinner(text);
  spinner.start();
  return spinner;
}

export function stopSpinner(): void {
  if (activeSpinner) {
    activeSpinner.stop();
    activeSpinner = null;
  }
}

export function succeedSpinner(text?: string): void {
  if (activeSpinner) {
    activeSpinner.succeed(text);
    activeSpinner = null;
  }
}

export function failSpinner(text?: string): void {
  if (activeSpinner) {
    activeSpinner.fail(text);
    activeSpinner = null;
  }
}

export function updateSpinner(text: string): void {
  if (activeSpinner) {
    activeSpinner.text = text;
  }
}
