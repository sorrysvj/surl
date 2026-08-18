# Contributing to surl

Thank you for your interest in contributing to surl! This document provides guidelines and instructions for contributing.

## Code of Conduct

Please be respectful and constructive in all interactions.

## Getting Started

### Prerequisites

- Node.js 18+
- npm or pnpm
- Git

### Setup

1. Fork the repository
2. Clone your fork:
   ```bash
   git clone https://github.com/YOUR_USERNAME/surl.git
   cd surl
   ```
3. Install dependencies:
   ```bash
   npm install
   ```
4. Create a branch:
   ```bash
   git checkout -b feature/your-feature
   ```

## Development

### Running in Development Mode

```bash
npm run dev -- https://example.com
```

### Building

```bash
npm run build
```

### Testing

```bash
# Run all tests
npm test

# Run tests in watch mode
npm run test:watch

# Run tests with coverage
npm run test:coverage
```

### Linting

```bash
# Check for issues
npm run lint

# Auto-fix issues
npm run lint:fix
```

### Formatting

```bash
# Format code
npm run format

# Check formatting
npm run format:check
```

## Code Style

- Use TypeScript with strict mode
- Avoid `any` type
- Use meaningful variable and function names
- Add JSDoc comments for public APIs
- Keep functions small and focused
- Write tests for new features

## Pull Requests

1. Update tests if needed
2. Ensure all tests pass
3. Ensure linting passes
4. Update documentation if needed
5. Write a clear PR description
6. Reference any related issues

## Reporting Issues

- Use the issue templates
- Include reproduction steps
- Include your environment (OS, Node.js version)
- Include relevant error messages

## Feature Requests

- Check existing issues first
- Describe the use case
- Explain why existing features don't work

## Questions

Feel free to open an issue for questions about the codebase.

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
