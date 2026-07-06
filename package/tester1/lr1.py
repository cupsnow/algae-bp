import logging, sys, os

logging.basicConfig(
    level=logging.DEBUG,
    format="[%(asctime)s][%(levelname)s][%(funcName)s][#%(lineno)d]%(message)s",
)
logger = logging.getLogger(os.path.basename(__file__))


def main():
    # define a simple dataset of (x, y) pairs
    dataset = [ (1, 3), (2, 5), (3, 7), (4, 9) ]

    # initial parameters for y = Ax + B (a linear regression model)
    a = 0
    b = 0

    # prediction function (for the linear regression model)
    def predict(x):
        return a * x + b

    # apply initial parameters and list all transitions
    for x, y in dataset:
        logger.debug(f"Input: {x}, Actual: {y}, Predicted: {predict(x)}")

    ''' Result
    Input: 1, Actual: 3, Predicted: 0
    Input: 2, Actual: 5, Predicted: 0
    Input: 3, Actual: 7, Predicted: 0
    Input: 4, Actual: 9, Predicted: 0
    '''

    # means squared error (MSE) function (measure of how well the model fits the data)
    def error_calc(a2=None, b2=None):
        nonlocal a
        nonlocal b
        if a2 is not None:
            a = a2
        if b2 is not None:
            b = b2

        total = 0
        for x, y in dataset:
            total += (predict(x) - y) ** 2
        return total

    # manually adjust parameters and show the error
    logger.debug(f"Parameters: a={a}, b={b}, Error={error_calc()}") # error 164
    logger.debug(f"Parameters: a={0.001}, b={b}, Error={error_calc(0.001)}") # error 163.86
    logger.debug(f"Parameters: a={1}, b={b}, Error={error_calc(1)}") # error 54
    logger.debug(f"Parameters: a={2}, b={b}, Error={error_calc(2)}") # error 4
    logger.debug(f"Parameters: a={2}, b={1}, Error={error_calc(2, 1)}") # error 0

    # back to initial parameters
    a = b = 0

    # increasing A; here we already know increasing A will reduce error
    best_error = 999999
    while True:
        a += 0.1
        error = error_calc()
        logger.debug(f"Parameters: a={a:.2f}, b={b:.2f}, Error={error:.2f}")
        if error > best_error:
            break
        best_error = error

    ''' Result
    Parameters: a=0.10, b=0.00, Error=150.30
    Parameters: a=0.20, b=0.00, Error=137.20
    Parameters: a=0.30, b=0.00, Error=124.70
    ...
    Parameters: a=2.20, b=0.00, Error=1.20
    Parameters: a=2.30, b=0.00, Error=0.70
    Parameters: a=2.40, b=0.00, Error=0.80
    '''

    a = 2
    b = 0
    best_error = 999999

    # increasing B; here we already know increasing B will reduce error
    while True:
        b += 0.1
        error = error_calc()
        logger.debug(f"Parameters: a={a:.2f}, b={b:.2f}, Error={error:.2f}")
        if error > best_error:
            break
        best_error = error

    ''' Result
    Parameters: a=2.00, b=0.10, Error=3.24
    Parameters: a=2.00, b=0.20, Error=2.56
    Parameters: a=2.00, b=0.30, Error=1.96
    Parameters: a=2.00, b=0.40, Error=1.44
    Parameters: a=2.00, b=0.50, Error=1.00
    Parameters: a=2.00, b=0.60, Error=0.64
    Parameters: a=2.00, b=0.70, Error=0.36
    Parameters: a=2.00, b=0.80, Error=0.16
    Parameters: a=2.00, b=0.90, Error=0.04
    Parameters: a=2.00, b=1.00, Error=0.00
    Parameters: a=2.00, b=1.10, Error=0.04
    '''

    # gradient function (measure of how much the error changes with respect to increasing A)
    # when return negative value means increasing A will reduce error
    def gradient_calc(step = 0.001, step2 = 0.001):
        nonlocal a
        error_a = error_calc()
        a += step
        error_a2 = error_calc()
        a -= step

        nonlocal b
        error_b = error_calc()
        b += step2
        error_b2 = error_calc()
        b -= step2

        return (error_a2 - error_a) / step, (error_b2 - error_b) / step2

    # reset parameters
    a = b = 0
    
    step = 0.001
    ga, gb = gradient_calc(step, step)
    logger.debug(f"Parameters: a={a:.2f}, b={b:.2f}, gradient=({ga:.2f}, {gb:.2f}), error={error_calc():.2f}")
    ''' Result
    Parameters: a=0.00, b=0.00, gradient=(-139.97, -48.00), error=164.00
    '''

    learning_rate = 0.01
    for i in range(100):
        ga, gb = gradient_calc(step, step)
        old_a, old_b = a, b
        a -= learning_rate * ga
        b -= learning_rate * gb
        logger.debug(f"Parameters: a={old_a:.2f}, b={old_b:.2f}, gradient=({ga:.2f}, {gb:.2f}), error={error_calc():.2f}, a -> {a:.2f}, b -> {b:.2f}")
    '''
    Parameters: a=0.00, b=0.00, gradient=(-139.97, -48.00), error=18.14, a -> 1.40, b -> 0.48
    Parameters: a=1.40, b=0.48, gradient=(-46.39, -16.16), error=2.05, a -> 1.86, b -> 0.64
    Parameters: a=1.86, b=0.64, gradient=(-15.32, -5.59), error=0.27, a -> 2.02, b -> 0.70
    ...
    Parameters: a=2.03, b=0.91, gradient=(0.04, -0.11), error=0.01, a -> 2.03, b -> 0.91
    Parameters: a=2.03, b=0.91, gradient=(0.04, -0.11), error=0.01, a -> 2.03, b -> 0.91
    Parameters: a=2.03, b=0.91, gradient=(0.04, -0.11), error=0.01, a -> 2.03, b -> 0.91
    Parameters: a=2.03, b=0.91, gradient=(0.04, -0.11), error=0.00, a -> 2.03, b -> 0.91
    Parameters: a=2.03, b=0.91, gradient=(0.04, -0.11), error=0.00, a -> 2.03, b -> 0.92
    Parameters: a=2.03, b=0.92, gradient=(0.04, -0.11), error=0.00, a -> 2.03, b -> 0.92
    Parameters: a=2.03, b=0.92, gradient=(0.04, -0.11), error=0.00, a -> 2.03, b -> 0.92
    '''

if __name__ == "__main__":
    main()
