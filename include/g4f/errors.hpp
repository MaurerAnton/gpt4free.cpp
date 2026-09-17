// Port of g4f/errors.py.
// Upstream: https://github.com/xtekky/gpt4free (GPL-3.0)
#pragma once

#include <stdexcept>
#include <string>

namespace g4f {

class G4FError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ProviderNotFoundError : public G4FError {
public:
    using G4FError::G4FError;
};
class ProviderNotWorkingError : public G4FError {
public:
    using G4FError::G4FError;
};
class StreamNotSupportedError : public G4FError {
public:
    using G4FError::G4FError;
};
class ModelNotFoundError : public G4FError {
public:
    using G4FError::G4FError;
};
class ModelNotAllowedError : public G4FError {
public:
    using G4FError::G4FError;
};
class RetryProviderError : public G4FError {
public:
    using G4FError::G4FError;
};
class RetryNoProviderError : public G4FError {
public:
    using G4FError::G4FError;
};
class VersionNotFoundError : public G4FError {
public:
    using G4FError::G4FError;
};
class MissingRequirementsError : public G4FError {
public:
    using G4FError::G4FError;
};
class NestAsyncioError : public MissingRequirementsError {
public:
    using MissingRequirementsError::MissingRequirementsError;
};
class MissingAuthError : public G4FError {
public:
    using G4FError::G4FError;
};
class PaymentRequiredError : public G4FError {
public:
    using G4FError::G4FError;
};
class NoMediaResponseError : public G4FError {
public:
    using G4FError::G4FError;
};
class ResponseError : public G4FError {
public:
    using G4FError::G4FError;
};

} // namespace g4f
